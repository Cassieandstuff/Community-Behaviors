#include "PCH.h"

#include <platform/hooks/takeover/SchemaShim.h>

#include <platform/hooks/registry/Registry.h>

#include <cstring>
#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CB::platform::hooks::detail {

    std::uintptr_t ResolveShimSlot(unsigned slot, const engine::Symbol* const* funcs, std::size_t count, std::uintptr_t* slots)
    {
        const engine::Symbol* symbol = slot < count ? funcs[slot] : nullptr;
        if (!symbol || !engine::Available(*symbol)) {
            const std::string what = std::format("Community Behaviors: schema function {} was called but has no address on this runtime",
                                                 symbol ? symbol->name : "?");
            LOG_ERROR("{}", what);
            SKSE::stl::report_and_fail(what);
        }
        const std::uintptr_t to = engine::Resolve(*symbol);
        slots[slot]             = to;
        LOG_INFO("schema shim: {} bound to 0x{:X}", symbol->name, to);
        return to;
    }

    void CopyShimData(const ShimData* rows, std::size_t count)
    {
        for (std::size_t i = 0; i < count; i++) {
            const ShimData& r = rows[i];
            if (!engine::Available(*r.symbol)) {
                LOG_WARN("schema shim: {} has no address on this runtime; the plugin's copy stays zero", r.symbol->name);
                continue;
            }
            std::memcpy(r.to, reinterpret_cast<const void*>(engine::Resolve(*r.symbol)), r.size);
        }
        if (count) LOG_INFO("schema shim: {} const global(s) copied from the exe", count);
    }

    namespace {

        struct Image {
            std::uintptr_t base = 0, size = 0, text = 0, textEnd = 0, rdata = 0, rdataEnd = 0;
            explicit Image(HMODULE module) : base(reinterpret_cast<std::uintptr_t>(module))
            {
                const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
                const auto* sec = IMAGE_FIRST_SECTION(nt);
                size            = nt->OptionalHeader.SizeOfImage;
                for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
                    const std::string_view name(reinterpret_cast<const char*>(sec->Name), strnlen(reinterpret_cast<const char*>(sec->Name), 8));
                    if (name == ".text" && sec->Misc.VirtualSize > textEnd - text) { text = base + sec->VirtualAddress; textEnd = text + sec->Misc.VirtualSize; }
                    if (name == ".rdata") { rdata = base + sec->VirtualAddress; rdataEnd = rdata + sec->Misc.VirtualSize; }
                }
            }
            bool InRdata(std::uintptr_t a) const { return a >= rdata && a + 24 <= rdataEnd; }
            bool InText(std::uintptr_t a) const { return a >= text && a < textEnd; }
        };

        /// @brief Every vtable with RTTI in `image`: "class name at offset" -> its first slot. Null when the key repeats.
        std::unordered_map<std::string, const std::uintptr_t*> Vtables(const Image& image)
        {
            std::unordered_map<std::string, const std::uintptr_t*> out;
            for (std::uintptr_t at = image.rdata; at + 16 <= image.rdataEnd; at += 8) {
                const auto col = *reinterpret_cast<const std::uintptr_t*>(at);
                if (!image.InRdata(col) || !image.InText(*reinterpret_cast<const std::uintptr_t*>(at + 8))) continue;
                const auto* c = reinterpret_cast<const std::uint32_t*>(col);
                if (c[0] != 1 || image.base + c[5] != col || c[3] >= image.size) continue;
                const char* name = reinterpret_cast<const char*>(image.base + c[3] + 0x10);
                if (std::strncmp(name, ".?AV", 4) != 0) continue;
                const auto [it, added] = out.try_emplace(std::string(name) + "+" + std::to_string(c[1]), reinterpret_cast<const std::uintptr_t*>(at + 8));
                if (!added) it->second = nullptr;
            }
            return out;
        }

        /// @brief Makes the plugin's copy of each exe class's vtable the exe's, slot for slot.
        /// @note The SDK's headers declare fewer overrides than the exe's classes have, so a compiled vtable inherits
        ///       where the game overrides: an object the plugin builds would silently run the base's function. Each slot
        ///       now enters the exe's function, which is the shipped body when the takeover jumps it.
        /// @note A CB class has no exe vtable of its name and keeps its own.
        void AdoptExeVtables()
        {
            HMODULE self = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&AdoptExeVtables), &self);
            const Image exe(GetModuleHandleA(nullptr)), plugin(self);
            const auto  exeTables = Vtables(exe);
            std::size_t adopted = 0, slots = 0, shorter = 0;
            for (const auto& [key, own] : Vtables(plugin)) {
                const auto theirs = exeTables.find(key);
                if (!own || theirs == exeTables.end() || !theirs->second) continue;
                std::size_t n = 0, ownCount = 0, exeCount = 0;
                const auto  isSlot = [](const Image& i, const std::uintptr_t* t, std::size_t k) {
                    const auto a = reinterpret_cast<std::uintptr_t>(t + k);
                    return a + 8 <= i.rdataEnd && i.InText(t[k]);
                };
                while (isSlot(exe, theirs->second, exeCount)) ++exeCount;
                while (isSlot(plugin, own, ownCount)) ++ownCount;
                n = (std::min)(exeCount, ownCount);
                if (ownCount > exeCount) ++shorter;
                DWORD old = 0;
                auto* target = const_cast<std::uintptr_t*>(own);
                if (!n || !VirtualProtect(target, n * 8, PAGE_READWRITE, &old)) continue;
                std::memcpy(target, theirs->second, n * 8);
                VirtualProtect(target, n * 8, old, &old);
                ++adopted;
                slots += n;
            }
            LOG_INFO("schema shim: {} plugin vtable(s) adopted the exe's ({} slots; {} longer than the exe's)", adopted, slots, shorter);
        }

    }  // namespace

    void BindShimRefs(const ShimRef* rows, std::size_t count)
    {
        std::size_t bound = 0;
        for (std::size_t i = 0; i < count; i++) {
            const ShimRef& r = rows[i];
            if (!engine::Available(*r.symbol)) {
                LOG_WARN("schema shim: {} has no address on this runtime; a use of it faults", r.symbol->name);
                continue;
            }
            *r.slot = reinterpret_cast<void*>(engine::Resolve(*r.symbol));
            ++bound;
        }
        if (count) LOG_INFO("schema shim: {} of {} exe global(s) bound by reference", bound, count);
        AdoptExeVtables();
    }

}  // namespace CB::platform::hooks::detail
