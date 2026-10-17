#include "PCH.h"

#include "core/hooks/takeover/Takeover.h"

#include "core/debug/Settings.h"
#include "core/hooks/takeover/Cell.h"
#include "core/hooks/takeover/ImageSource.h"
#include "core/hooks/takeover/NearExe.h"

#include <PluginLogger.h>

#include <codec/image/RuntimeImage.h>
#include <text/Text.h>

#include <set>

namespace CB::core::hooks {

    namespace {

        constexpr const char* kImageFile = "Data/community_behaviors/runtime/havok-runtime.cbimg";
        constexpr const char* kSection   = "HavokRuntime";
        /// @brief A cell stub: `jmp [rip+0]`, the 8-byte cell right after it, padded to 16.
        constexpr std::size_t kStub = 16;

        struct Installed {
            std::string_view            name;
            std::string_view            mangled;
            std::uintptr_t              at = 0;
            std::uint32_t               length = 0;
            std::array<std::uint8_t, 5> original{};
            std::array<std::uint8_t, 5> jump{};
            /// @brief Vanilla's bytes past the jump as they stood at install: a later change is someone else's patch.
            std::vector<std::uint8_t>   body;
            std::uintptr_t*             cell = nullptr;
            /// @brief What the code source placed in the cell; the cell holding anything else means it is overridden.
            std::uintptr_t              impl = 0;
        };

        struct State {
            std::vector<std::unique_ptr<ICodeSource>> sources;
            std::vector<Installed>                    live;
        };

        State& Takeover()
        {
            static State s;
            return s;
        }

        std::uint64_t Hash(std::uintptr_t at, std::size_t n)
        {
            return CB::engine::codec::image::Fnv1a64(reinterpret_cast<const std::uint8_t*>(at), n);
        }

        /// @return the module holding `a` and the offset into it ("OpenAnimationReplacer.dll+0x1234"), or the bare address.
        std::string ModuleOf(std::uintptr_t a)
        {
            HMODULE mod = nullptr;
            wchar_t path[MAX_PATH]{};
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(a), &mod) &&
                GetModuleFileNameW(mod, path, MAX_PATH))
                return std::format("{}+0x{:X}", std::filesystem::path(path).filename().string(), a - reinterpret_cast<std::uintptr_t>(mod));
            return std::format("0x{:X}", a);
        }

        /// @return where another plugin's patch sits in `in`'s body and, for a rewritten call/jmp rel32, the module it now
        ///         reaches (through an SKSE trampoline's `jmp [rip]` stub if that is what it calls).
        std::string DescribePatch(const Installed& in)
        {
            const auto* now = reinterpret_cast<const std::uint8_t*>(in.at + 5);
            std::size_t k   = 0;
            while (k < in.body.size() && now[k] == in.body[k]) ++k;
            if (k == in.body.size()) return "body unchanged";
            const std::size_t off = 5 + k;
            for (std::size_t o = off > 9 ? off - 4 : 5; o < off; ++o) {
                const std::uint8_t op = *reinterpret_cast<const std::uint8_t*>(in.at + o);
                if (op != 0xE8 && op != 0xE9) continue;
                std::int32_t disp;
                std::memcpy(&disp, reinterpret_cast<const void*>(in.at + o + 1), 4);
                std::uintptr_t dst = in.at + o + 5 + disp;
                const auto*    stub = reinterpret_cast<const std::uint8_t*>(dst);
                if (stub[0] == 0xFF && stub[1] == 0x25 && stub[2] == 0 && stub[3] == 0 && stub[4] == 0 && stub[5] == 0)
                    std::memcpy(&dst, stub + 6, 8);
                return std::format("{} at +0x{:X} now reaches {}", op == 0xE8 ? "call" : "jmp", o, ModuleOf(dst));
            }
            return std::format("first change at +0x{:X}", off);
        }

        std::set<std::string, std::less<>> SplitList(std::string_view s)
        {
            std::set<std::string, std::less<>> out;
            while (!s.empty()) {
                const auto comma = s.find(',');
                const auto item  = CB::engine::text::Trim(s.substr(0, comma));
                if (!item.empty()) out.emplace(item);
                s = comma == std::string_view::npos ? std::string_view() : s.substr(comma + 1);
            }
            return out;
        }

    }  // namespace

    std::uintptr_t* FindCell(std::string_view mangled, std::string* why)
    {
        std::uintptr_t* found = nullptr;
        std::size_t     hits  = 0;
        for (const auto& in : Takeover().live)
            if (in.mangled == mangled) found = in.cell, ++hits;
        if (hits == 1) return found;
        if (why) *why = hits == 0 ? "not installed by the takeover" : "names " + std::to_string(hits) + " installed functions";
        return nullptr;
    }

    namespace {

        void InstallFunctions()
        {
            if (!debug::FlagOn("havok_runtime")) {
                LOG_INFO("havok runtime: off ([{}] bEnable) — vanilla runs", kSection);
                return;
            }
            auto image = ImageSource::Load(kImageFile);
            if (!image) return;
            std::byte* stubs = AllocateNearExe(image->Count() * kStub);
            if (!stubs) {
                LOG_ERROR("havok runtime: no free memory near the exe for the cell table — vanilla runs");
                return;
            }

            State&            st   = Takeover();
            const std::string only = debug::Value(kSection, "sOnly");
            const auto        keep = SplitList(only);
            const std::uintptr_t exe = REL::Module::get().base();
            std::size_t classes = 0, off = 0, patched = 0, tooShort = 0;
            for (const auto& cls : image->Classes()) {
                const std::string choice = debug::Value(kSection, cls, image->Name());
                if ((!keep.empty() && !keep.contains(cls)) || choice == "off") {
                    ++off;
                    continue;
                }
                if (choice != image->Name()) {
                    LOG_WARN("havok runtime: [{}] {}={} names no code source (image, off) — vanilla runs it", kSection, cls, choice);
                    ++off;
                    continue;
                }
                ++classes;
                for (const auto& r : image->Provide(cls)) {
                    if (r.vanillaLength < 5) {
                        ++tooShort;
                        continue;
                    }
                    const std::uintptr_t at = exe + r.vanillaRva;
                    if (Hash(at, r.vanillaLength) != r.vanillaHash) {
                        LOG_INFO("havok runtime: {} is not as shipped (another plugin's hook?) — vanilla runs it", r.name);
                        ++patched;
                        continue;
                    }
                    std::byte* const     stub = stubs + st.live.size() * kStub;
                    const std::uintptr_t s    = reinterpret_cast<std::uintptr_t>(stub);
                    const std::int64_t   rel  = static_cast<std::int64_t>(s) - static_cast<std::int64_t>(at + 5);
                    if (rel < INT32_MIN || rel > INT32_MAX) {
                        LOG_ERROR("havok runtime: {}'s cell is out of jump reach — vanilla runs it", r.name);
                        continue;
                    }
                    constexpr std::uint8_t kJmpRip[6] = { 0xFF, 0x25, 0, 0, 0, 0 };
                    std::memcpy(stub, kJmpRip, sizeof kJmpRip);
                    std::memcpy(stub + 6, &r.entry, 8);
                    Installed in{ r.name, r.mangled, at, r.vanillaLength };
                    in.cell = reinterpret_cast<std::uintptr_t*>(stub + 6);
                    in.impl = r.entry;
                    std::memcpy(in.original.data(), reinterpret_cast<const void*>(at), 5);
                    in.jump[0]     = 0xE9;
                    const auto r32 = static_cast<std::int32_t>(rel);
                    std::memcpy(in.jump.data() + 1, &r32, 4);
                    in.body.assign(reinterpret_cast<const std::uint8_t*>(at + 5), reinterpret_cast<const std::uint8_t*>(at + r.vanillaLength));
                    REL::safe_write(at, in.jump.data(), in.jump.size());
                    st.live.push_back(in);
                }
            }
            FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
            st.sources.push_back(std::move(image));
            LOG_INFO("havok runtime: {} functions now run CB's code across {} classes; {} classes left to vanilla by settings, {} functions "
                     "patched by someone else, {} too short to jump",
                     st.live.size(), classes, off, patched, tooShort);
        }

    }  // namespace

    void InstallTakeover()
    {
        InstallFunctions();
        detail::InstallCells();
    }

    void RecheckTakeover()
    {
        State& st = Takeover();
        if (st.live.empty()) return;
        std::size_t restored = 0, wrapped = 0, lost = 0;
        for (const auto& in : st.live) {
            const bool ours = std::memcmp(reinterpret_cast<const void*>(in.at), in.jump.data(), 5) == 0;
            const bool tail = std::memcmp(reinterpret_cast<const void*>(in.at + 5), in.body.data(), in.body.size()) == 0;
            if (ours && tail) continue;
            if (!ours && tail) {
                ++wrapped;   // an entry detour on top of the jump: it still reaches CB's code
                continue;
            }
            if (ours) {
                LOG_WARN("havok runtime: {}'s vanilla body was patched after CB loaded ({}) — vanilla (with that patch) runs it", in.name, DescribePatch(in));
                if (*in.cell != in.impl) LOG_WARN("havok runtime: {} is handed back — the feature overrides on its cell stop firing", in.name);
                REL::safe_write(in.at, in.original.data(), in.original.size());
                ++restored;
            } else {
                LOG_WARN("havok runtime: {} was patched at its entry and in its body after CB loaded ({}) — left as is", in.name, DescribePatch(in));
                ++lost;
            }
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        LOG_INFO("havok runtime: recheck — {} of {} functions still run CB's code ({} behind another entry detour); {} handed back to vanilla, {} "
                 "unclear",
                 st.live.size() - restored - lost, st.live.size(), wrapped, restored, lost);
    }

}  // namespace CB::core::hooks
