#pragma once

#include <platform/hooks/registry/Symbol.h>

#include <cstddef>
#include <cstdint>

/// @file
/// @brief The schema link shim: what makes havok's declarations usable from the plugin as written.
/// @note The plugin compiles the CB SDK (lib/cb-sdk), whose unmatched functions and exe-owned globals are declared
///       but undefined here. Every registry row with `mangled` (engine.yaml + the SDK's) closes that gap at link time
///       (cmake/EmbedEngineRegistry.cmake generates the shim): a function's decorated name binds (/alternatename) to a
///       jump stub that resolves vanilla's entry on first call and lands there, unless the SDK defines it; an exe
///       global the SDK declares by reference (HK_EXE_GLOBAL) binds to a slot holding the exe's address; a const
///       global's decorated name binds to a copy of vanilla's bytes.
/// @note Drift gate: a declaration whose signature changes decorates differently, so its old binding no longer
///       matches and the plugin fails to link until engine.yaml's `mangled` is updated.

namespace CB::platform::hooks {

    /// @brief Copies every shimmed const global from the exe. Call first in SKSEPluginLoad, before any feature reads
    ///        one (they read zeros until then).
    void InstallSchemaShim();

    namespace detail {

        struct ShimData {
            const engine::Symbol* symbol;
            void*                 to;
            std::size_t           size;
        };

        /// @brief An exe global the SDK declares by reference (HK_EXE_GLOBAL): `slot` is the reference's storage.
        struct ShimRef {
            const engine::Symbol* symbol;
            void**                slot;
        };

        /// @brief Resolves function slot `slot` on first call and stores it; fatal when the running runtime has no pin.
        std::uintptr_t ResolveShimSlot(unsigned slot, const engine::Symbol* const* funcs, std::size_t count, std::uintptr_t* slots);
        void           CopyShimData(const ShimData* rows, std::size_t count);
        /// @warning Until this runs every such reference is null: an SDK static initializer that reads one sees null.
        void           BindShimRefs(const ShimRef* rows, std::size_t count);

        /// @return the shimmed functions, in slot order (cb_impl_table / cb_stub_table's order).
        const engine::Symbol* const* ShimFunctions();
        const char* const*           ShimFunctionMangled();
        /// @return the decorated names of the bodies the SDK ships, null-terminated.
        const char* const*           ShimShipped();
        /// @return each shipped body's match tier ("exact", "structural", "parked"), in ShimShipped's order.
        const char* const*           ShimShippedTier();
        std::size_t                  ShimFunctionCount();

    }  // namespace detail

}  // namespace CB::platform::hooks
