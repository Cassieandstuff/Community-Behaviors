#pragma once

#include <cstdint>

// BR compile-time WATERMARK — an unforgeable "this graph was served by Community Behaviors" tell.
//
// The Resolver injects an INT32 graph variable named kWatermarkVar, stamped with kWatermarkValue,
// into every behavior graph it compiles (see Resolver::Resolve). At runtime a probe reads it back
// off the actor's root graph with GetGraphVariableInt: a vanilla (un-served) graph simply doesn't
// have the variable, so a `true` return is proof BR's compiled bytes are loaded. The value carries
// the plugin version (major*10000 + minor*100 + patch -> 0.3.0.0 = 30000), shown by the debug overlay.
// It no longer decides cache reuse: the cache key (CacheKey.h) hashes the DLL itself, so any rebuild
// recompiles and the served graphs always carry this build's value.

namespace CB::core::resolve {

    inline constexpr const char* kWatermarkVar   = "BR_Watermark";
    inline constexpr std::int32_t kWatermarkValue = 30100;   // 0.3.1.0 — case-preserving clip names (BR-16 root-motion fix)

}  // namespace CB::core::resolve
