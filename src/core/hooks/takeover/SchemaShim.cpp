#include "PCH.h"

#include <core/hooks/takeover/SchemaShim.h>

#include <core/hooks/registry/Registry.h>

#include <cstring>

namespace CB::core::hooks::detail {

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

}  // namespace CB::core::hooks::detail
