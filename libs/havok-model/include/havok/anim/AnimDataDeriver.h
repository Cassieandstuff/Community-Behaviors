#pragma once
// AnimDataDeriver — derive an animationdatasinglefile clip list from the behavior graph.
//
// This is part (B) of the Skyrim.hky model (see br-runtime-is-the-feature): the
// animationdata clip metadata is a PROJECTION of the behavior graph's hkbClipGenerator
// nodes, not independent authored data. Written ONCE here in havok-core so the SAME code
// runs offline (the animdata-derive-check oracle, byte-gated against vanilla) and at
// runtime (ServeAnimData builds the served clip list from the compiled graph).
//
// Empirically byte-exact against vanilla DefaultMale (study 2026-08-14): 99.1% of triggers
// reproduced byte-for-byte from graph + carried motion duration; the rest were name-collision
// study artifacts (single-behavior decompile). Field provenance:
//   name           = clip generator name (verbatim)
//   animIndex      = roster.indexOf(animationName)  (the char's animationNames position;
//                    NOT hkbClipGenerator.animationBindingIndex, which is -1 in vanilla)
//   playbackSpeed  = %g(graph value)
//   cropStart/End  = %g(graph value)
//   triggers       = event name + ":" + %g(t), where
//                       eff = snap30(motionDuration) / playbackSpeed
//                       t   = clamp(relativeToEndOfClip ? eff + localTime : localTime, 0, eff)
//                    sorted ascending by t (the cache's order).
// Motion records themselves are NOT derived (proprietary toolchain extraction) — they are
// carried from the Skyrim.hky master and only their DURATION feeds the trigger clamp here.

#include "havok/anim/AnimationData.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace havok::animdata {

    // A clip generator's fields as they matter to animationdata derivation, taken from the
    // compiled/decompiled behavior graph (event names already resolved to strings).
    struct DeriveClipInput {
        std::string name;
        std::string animationName;         // e.g. "Animations\\1HM_TurnRight180.HKX"
        double      playbackSpeed = 1.0;
        double      cropStart     = 0.0;
        double      cropEnd       = 0.0;

        struct Trigger {
            std::string event;
            double      localTime           = 0.0;
            bool        relativeToEndOfClip = false;
            // True for events merged from the animation's annotation track (vs authored on the
            // clip generator). Annotation events are post-processed to match the cache: AnimObj*
            // events are dropped, and only the name before the first '.' is kept (the ".payload"
            // is consumed from the annotation at runtime). Clip-gen triggers are kept verbatim.
            bool        fromAnnotation      = false;
        };
        std::vector<Trigger> triggers;
    };

    // Bethesda's cache float convention: printf %g (6 significant digits, Windows 3-digit
    // exponent). Every derived numeric field is emitted through this.
    std::string FormatG(double v);

    // Snap a %g-rounded duration back to the 30fps frame grid so the effective-duration
    // trigger clamp reproduces the cache's full-precision value: round(d*30)/30.
    double SnapDuration30(double d);

    // Derive one project's animationdata clip list from its graph clip generators.
    //   roster            : the character's animationNames IN ORDER (animIndex = its index;
    //                       matched case-insensitively, as the runtime roster resolve does).
    //   motionDurByIndex  : animIndex -> carried motion duration (parsed from the master's
    //                       %g string); used only for the effective-duration trigger clamp.
    // A clip whose animationName is not in the roster gets animIndex "-1" and is recorded in
    // `unresolved` (never silently dropped). Clips keep input order.
    std::vector<ClipGenerator> DeriveClipList(
        const std::vector<DeriveClipInput>&           clips,
        const std::vector<std::string>&               roster,
        const std::unordered_map<int, double>&        motionDurByIndex,
        std::vector<std::string>*                     unresolved = nullptr);

}  // namespace havok::animdata
