#include "PCH.h"

#include "platform/hooks/takeover/DllSource.h"

#include "platform/hooks/registry/Registry.h"
#include "platform/hooks/takeover/SchemaShim.h"

#include <PluginLogger.h>

#include "core/debug/Settings.h"

#include <codec/image/RuntimeImage.h>

#include <string_view>
#include <unordered_set>

/// @brief cmake/EmbedEngineRegistry.cmake's tables, one QWORD per shim slot: what the slot's decorated name linked to,
///        and the slot's own stub (equal: the plugin has no definition of it).
extern "C" const std::uintptr_t cb_impl_table[];
extern "C" const std::uintptr_t cb_stub_table[];

namespace CB::platform::hooks {

    namespace {

        /// @brief The class a decorated member name belongs to (`?f@Class@@...`, `??0Class@@...`, `??_GClass@@...`): what
        ///        [HavokRuntime] <Class>= and sOnly name. A generated row's own name carries no class.
        std::string ClassOfDecorated(std::string_view m)
        {
            std::size_t at = 0;
            if (m.starts_with("??_")) at = 4;
            else if (m.starts_with("??")) at = 3;
            else if (const auto first = m.find('@'); first != std::string_view::npos) at = first + 1;
            const auto end = m.find('@', at);
            return std::string(m.substr(at, end == std::string_view::npos ? end : end - at));
        }

    }  // namespace

    DllSource::DllSource()
    {
        const auto* funcs   = detail::ShimFunctions();
        const auto* mangled = detail::ShimFunctionMangled();
        std::unordered_set<std::string_view> shipped;
        for (const char* const* s = detail::ShimShipped(); *s; ++s) shipped.insert(*s);
        /// @note [HavokRuntime] sFunctionsOff: `method@Class` entries, comma-separated, left to vanilla: bisects a class.
        std::vector<std::string> functionsOff;
        {
            std::string list = core::debug::Value("HavokRuntime", "sFunctionsOff");
            for (std::size_t at = 0; at < list.size();) {
                const auto end = (std::min)(list.find(',', at), list.size());
                if (end > at) functionsOff.push_back(list.substr(at, end - at));
                at = end + 1;
            }
        }
        for (std::size_t i = 0, n = detail::ShimFunctionCount(); i < n; ++i) {
            if (!cb_impl_table[i] || cb_impl_table[i] == cb_stub_table[i] || !engine::Available(*funcs[i])) continue;
            if (!mangled[i] || !shipped.contains(mangled[i])) continue;
            const std::string_view decorated = mangled[i];
            if (std::any_of(functionsOff.begin(), functionsOff.end(),
                            [&](const std::string& off) { return decorated.find(off) != std::string_view::npos; })) {
                LOG_INFO("havok runtime: {} left to vanilla by sFunctionsOff", decorated);
                continue;
            }
            slots_[ClassOfDecorated(mangled[i])].push_back(i);
            ++count_;
        }
    }

    std::vector<std::string> DllSource::Classes() const
    {
        std::vector<std::string> out;
        out.reserve(slots_.size());
        for (const auto& [cls, _] : slots_) out.push_back(cls);
        return out;
    }

    std::vector<Replacement> DllSource::Provide(std::string_view cls)
    {
        std::vector<Replacement> out;
        const auto it = slots_.find(cls);
        if (it == slots_.end()) return out;
        const auto*          funcs   = detail::ShimFunctions();
        const auto*          mangled = detail::ShimFunctionMangled();
        const std::uintptr_t exe   = REL::Module::get().base();
        for (const std::size_t i : it->second) {
            const std::uintptr_t at = engine::Resolve(*funcs[i]);
            DWORD64              base = 0;
            const auto*          fn   = RtlLookupFunctionEntry(at, &base, nullptr);
            if (!fn || base + fn->BeginAddress != at) {
                LOG_INFO("havok runtime: {} has no unwind entry of its own (a leaf?) — vanilla runs it", funcs[i]->name);
                continue;
            }
            const auto length = static_cast<std::uint32_t>(fn->EndAddress - fn->BeginAddress);
            out.push_back({ funcs[i]->name, mangled[i], static_cast<std::uint32_t>(at - exe), length,
                            CB::engine::codec::image::Fnv1a64(reinterpret_cast<const std::uint8_t*>(at), length), cb_impl_table[i] });
        }
        return out;
    }

}  // namespace CB::platform::hooks
