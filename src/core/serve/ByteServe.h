#pragma once

/// @file
/// @brief Byte-substitution serve for typed-hkx assets (project / character / behavior graph): CB's compiled
///        bytes load under the vanilla path and descriptor. Rationale in ByteServe.cpp.

namespace CB { class Resolver; }   // Resolver lives directly under CB (resolve stage)

namespace CB::core::serve {

    /// @brief Give the serve subscriber the resolver it asks for owned-asset swaps + on-demand project synth.
    /// @note Safe before or after Install(); a null resolver makes the subscriber a pure passthrough.
    void SetResolver(CB::Resolver* a_resolver);

    /// @brief Subscribe the serve to the typed-hkx resolver (Override on <platform/game/Resource.h>'s BSResourceAssetLoader_ResolveTypedHkx).
    /// @pre Called during SKSEPluginLoad, before CB::platform::hooks::InstallAll().
    void Install();

}  // namespace CB::core::serve
