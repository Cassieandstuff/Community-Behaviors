#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CB-API.h — Community Behaviors' SINGLE public API facade.
//
//   #include <CB-API.h>     // one door
//   link   cb::CB-API       // one target
//
// Any tool (Scene Editor, animation exporter, …) consumes exactly this. It exposes
// the three capabilities an external tool needs — and Community Behaviors itself is
// a FIRST-PARTY CONSUMER of it (we compile against <CB-API.h> too), so the surface
// can't rot: if it's insufficient for us, we feel it immediately.
//
//   cb::resolve  — read .hky bundles + resolve/merge a load order → the resolved model
//                  (identical to what the CB compiler does; single owner, no drift)
//   cb::anim     — decode / compile / EXPORT animations exactly as CB does
//   cb::schema   — the SchemaRegistry to interpret a resolved model / classify merges
//
// This is a RE-EXPORT, not a re-declaration: the types live ONCE in the engine
// modules; here they are only surfaced under cb:: names. No duplicate declarations,
// no drift. The whole surface is havok-core-FREE (no typed hkb*/hka*, no legacy).
//
// DELIBERATELY NOT EXPOSED — the Skyrim.hky master regen. It is first-party-only,
// by design, and this omission IS the enforcement:
//   (1) PROVENANCE — a master in the wild was minted by CB, not by a third-party
//       deriver that could diverge or tamper. There is no rogue master-maker.
//   (2) DISTRIBUTION — every tool must accept "the base arrives via MO2; the user
//       already has it." Tools CONSUME the master (cb::resolve reads it); they never
//       mint it. One canonical base, not N tool-specific ones.
// ─────────────────────────────────────────────────────────────────────────────

// resolve — the .hky document + the load-order merge + the resolved graph model
#include "havok/model/yaml/HkyArchive.h"
#include "havok/model/yaml/UnitSource.h"
#include "havok/model/yaml/YamlBehaviorLoader.h"
#include "havok/model/BehaviorData.h"

// schema — the Havok class-schema registry (merge classifier / model interpreter)
#include "havok-schema/HavokSchema.h"

// anim — decode/compile/export animations (schema-native; the CB pipeline itself)
#include "havok/anim/AnimationDef.h"
#include "havok/anim/AnimationYamlLoader.h"
#include "havok/anim/AnimationCompiler.h"
#include "havok/anim/AnimationDecompiler.h"

namespace cb {

// ── cb::resolve ──────────────────────────────────────────────────────────────
namespace resolve {
    using Archive       = havok::model::HkyArchive;         // a packed .hky bundle, decompressed in memory
    using UnitKind      = havok::model::HkyArchive::UnitKind;
    using UnitSource    = havok::model::IUnitSource;        // a unit's file view (disk dir OR in-memory .hky)
    using LoadOrder     = havok::model::YamlBehaviorLoader; // LoadMerged — the load-order merge
    using ResolvedGraph = havok::model::BehaviorData;       // the resolved, merged graph model
}  // namespace resolve

// ── cb::schema ───────────────────────────────────────────────────────────────
namespace schema {
    using Registry = havok::schema::SchemaRegistry;         // load Havok/ → classify/size/interpret classes
}  // namespace schema

// ── cb::anim ─────────────────────────────────────────────────────────────────
namespace anim {
    using Def             = havok::anim::AnimationDef;         // the authoring model (tracks/floats/annotations)
    using YamlLoader      = havok::anim::AnimationYamlLoader;  // animation.yaml ↔ Def
    using CompileResult   = havok::anim::AnimCompileResult;    // compile → packfile bytes
    using DecompileResult = havok::anim::AnimDecompileResult;  // .hkx bytes → animation.yaml

    using havok::anim::CompileAnimation;        // Def → .hkx bytes (schema-native)
    using havok::anim::CompileAnimationToFile;  // Def → .hkx file
    using havok::anim::DecompileAnimation;      // .hkx bytes → animation.yaml (the export leg)
}  // namespace anim

}  // namespace cb
