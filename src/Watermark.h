#pragma once

#include <cstdint>

// BR compile-time WATERMARK — an unforgeable "this graph was served by Community Behaviors" tell.
//
// The Resolver injects an INT32 graph variable named kWatermarkVar, stamped with kWatermarkValue,
// into every behavior graph it compiles (see Resolver::Resolve). At runtime a probe reads it back
// off the actor's root graph with GetGraphVariableInt: a vanilla (un-served) graph simply doesn't
// have the variable, so a `true` return is proof BR's compiled bytes are loaded. The value carries
// the plugin version so a mismatch (variable present but value != kWatermarkValue) flags a STALE
// on-disk cache from an older build — the cheap stand-in until real fingerprint invalidation lands.
//
// kWatermarkValue is a CACHE/BUILD FINGERPRINT — bump it on ANY change that alters compiled graph
// output (a new compile pass or injected variable, not only a plugin-version bump), so CachePresent()
// forces a regen and the served cache always matches the running DLL. Base it on the plugin version
// (major*10000 + minor*100 + patch -> 0.3.0.0 = 30000) and add a build rev when compiled output
// changes between version bumps. A fresh cache regen restamps every graph.

namespace CB::watermark {

    inline constexpr const char* kWatermarkVar   = "BR_Watermark";
    inline constexpr std::int32_t kWatermarkValue = 30100;   // 0.3.1.0 — case-preserving clip names (BR-16 root-motion fix)

}  // namespace CB::watermark
