#include "PCH.h"

#include "core/hooks/takeover/Takeover.h"

#include "core/debug/Settings.h"
#include "core/hooks/takeover/ImageSource.h"

#include <PluginLogger.h>

#include <codec/image/RuntimeImage.h>
#include <text/Text.h>

#include <set>

namespace CB::core::hooks {

    namespace {

        constexpr const char* kImageFile = "Data/community_behaviors/runtime/havok-runtime.cbimg";
        constexpr const char* kSection   = "HavokRuntime";

        struct Installed {
            std::string_view            name;
            std::uintptr_t              at = 0;
            std::uint32_t               length = 0;
            std::array<std::uint8_t, 5> original{};
            std::array<std::uint8_t, 5> jump{};
            /// @brief Vanilla's bytes past the jump as they stood at install: a later change is someone else's patch.
            std::uint64_t               tailHash = 0;
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

    void InstallTakeover()
    {
        if (!debug::FlagOn("havok_runtime")) {
            LOG_INFO("havok runtime: off ([{}] bEnable) — vanilla runs", kSection);
            return;
        }
        auto image = ImageSource::Load(kImageFile);
        if (!image) return;

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
                const std::int64_t rel = static_cast<std::int64_t>(r.entry) - static_cast<std::int64_t>(at + 5);
                if (rel < INT32_MIN || rel > INT32_MAX) {
                    LOG_ERROR("havok runtime: {}'s replacement is out of jump reach — vanilla runs it", r.name);
                    continue;
                }
                Installed in{ r.name, at, r.vanillaLength };
                std::memcpy(in.original.data(), reinterpret_cast<const void*>(at), 5);
                in.jump[0]     = 0xE9;
                const auto r32 = static_cast<std::int32_t>(rel);
                std::memcpy(in.jump.data() + 1, &r32, 4);
                in.tailHash = Hash(at + 5, r.vanillaLength - 5);
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

    void RecheckTakeover()
    {
        State& st = Takeover();
        if (st.live.empty()) return;
        std::size_t restored = 0, wrapped = 0, lost = 0;
        for (const auto& in : st.live) {
            const bool ours = std::memcmp(reinterpret_cast<const void*>(in.at), in.jump.data(), 5) == 0;
            const bool tail = Hash(in.at + 5, in.length - 5) == in.tailHash;
            if (ours && tail) continue;
            if (!ours && tail) {
                ++wrapped;   // an entry detour on top of the jump: it still reaches CB's code
                continue;
            }
            if (ours) {
                LOG_WARN("havok runtime: {}'s vanilla body was patched after CB loaded — vanilla (with that patch) runs it", in.name);
                REL::safe_write(in.at, in.original.data(), in.original.size());
                ++restored;
            } else {
                LOG_WARN("havok runtime: {} was patched at its entry and in its body after CB loaded — left as is", in.name);
                ++lost;
            }
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        LOG_INFO("havok runtime: recheck — {} of {} functions still run CB's code ({} behind another entry detour); {} handed back to vanilla, {} "
                 "unclear",
                 st.live.size() - restored - lost, st.live.size(), wrapped, restored, lost);
    }

}  // namespace CB::core::hooks
