#pragma once

#include <core/hooks/registry/Symbol.h>

#include <cstddef>
#include <cstdint>

/// @file
/// @brief The schema link shim: what makes schema/Havok's declarations usable from the plugin as written.
/// @note The plugin compiles schema/Havok's headers but none of its sources (those build into the takeover image),
///       so an out-of-line function or a vanilla global was declared but undefined here. Every engine.yaml row with
///       `mangled` closes that gap at link time (cmake/EmbedEngineRegistry.cmake generates the shim): a function's
///       decorated name binds (/alternatename) to a jump stub that resolves vanilla's entry on first call and lands
///       there, so it runs whatever the game runs (the takeover chain when the image owns it); a const global's
///       decorated name binds to a copy of vanilla's bytes.
/// @note Drift gate: a declaration whose signature changes decorates differently, so its old binding no longer
///       matches and the plugin fails to link until engine.yaml's `mangled` is updated.

namespace CB::core::hooks {

    /// @brief Copies every shimmed const global from the exe. Call first in SKSEPluginLoad, before any feature reads
    ///        one (they read zeros until then).
    void InstallSchemaShim();

    namespace detail {

        struct ShimData {
            const engine::Symbol* symbol;
            void*                 to;
            std::size_t           size;
        };

        /// @brief Resolves function slot `slot` on first call and stores it; fatal when the running runtime has no pin.
        std::uintptr_t ResolveShimSlot(unsigned slot, const engine::Symbol* const* funcs, std::size_t count, std::uintptr_t* slots);
        void           CopyShimData(const ShimData* rows, std::size_t count);

    }  // namespace detail

}  // namespace CB::core::hooks
