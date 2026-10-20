#pragma once

#include <core/hooks/registry/Registry.h>
#include <core/hooks/registry/SchemaIdentity.gen.h>

#include <format>
#include <string>
#include <type_traits>

/// @file
/// @brief Live<&global>(): a havok mutable global as the game holds it, named by its own declaration.
/// @note The schema shim binds a const global to a copy taken at load. A mutable one (a singleton, a cached setting)
///       would go stale as a copy, so its decorated name stays unbound: a direct use, an inline schema function's
///       included, fails to link, and Live reads the exe's variable through its engine.yaml pin instead.
/// @note `P` only selects GlobalIdentity<P> and types the result (decltype); Live never takes its value, so naming
///       the unbound variable in the template argument references nothing at link.

namespace CB::core::hooks {

    /// @return the exe's variable `P` names, typed as declared. Fatal on a runtime with no pin for it.
    template <auto P>
    auto& Live()
    {
        using T = std::remove_pointer_t<decltype(P)>;
        static T* const at = [] {
            const engine::Symbol& s = GlobalIdentity<P>::symbol;
            if (!engine::Available(s)) {
                const std::string what = std::format("Community Behaviors: schema global {} was read but has no address on this runtime", s.name);
                LOG_ERROR("{}", what);
                SKSE::stl::report_and_fail(what);
            }
            return reinterpret_cast<T*>(engine::Resolve(s));
        }();
        return *at;
    }

}  // namespace CB::core::hooks
