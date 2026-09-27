#pragma once

// ByteServe — unified byte-substitution serve layer for typed-hkx assets (project / character /
// behavior graph), the replacement for the `.br` project rename. See ByteServe.cpp for the full
// rationale. Currently an instrumentation pass (logs the resolver paths); the byte-substitution swap
// is wired in once the runtime path formats + routing are confirmed.

namespace CB { class Resolver; }   // Resolver lives directly under CB (resolve stage)

namespace CB::core::serve {

    // Give the serve hook the resolver it asks for owned-asset swaps + on-demand project synth.
    // Safe before or after Install(); a null resolver disables serving (pure passthrough).
    void SetResolver(CB::Resolver* a_resolver);

    // Install the FUN_140ba88b0 (BSResourceAssetLoader::Func3 resolver) hook. Call once at plugin
    // load, before actors spawn. AE-only addresses (guarded inside).
    void Install();

}  // namespace CB::core::serve
