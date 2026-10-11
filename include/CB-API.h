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
//   cb::resolve  — read .hky bundles + merge a load order → the merged graph, and derive a mod's
//                  patch (identical to what the CB compiler does; single owner, no drift)
//   cb::anim     — decode / compile / EXPORT animations exactly as CB does
//   cb::animdata — the animationdata (motion / root-motion) model + its YAML sidecar
//   cb::skeleton — decode / compile / EXPORT skeletons (schema-native; anim + ragdoll)
//   cb::schema   — the SchemaRegistry to interpret a resolved model / classify merges
//
// This is a RE-EXPORT, not a re-declaration: the types live ONCE in the engine
// modules; here they are only surfaced under cb:: names. No duplicate declarations,
// no drift. The whole surface is free of typed hkb*/hka* classes.
//
// DELIBERATELY NOT EXPOSED — the Skyrim.hky master regen. It is first-party-only,
// by design, and this omission IS the enforcement:
//   (1) PROVENANCE — a master in the wild was minted by CB, not by a third-party
//       deriver that could diverge or tamper. There is no rogue master-maker.
//   (2) DISTRIBUTION — every tool must accept "the base arrives via MO2; the user
//       already has it." Tools CONSUME the master (cb::resolve reads it); they never
//       mint it. One canonical base, not N tool-specific ones.
// ─────────────────────────────────────────────────────────────────────────────

// resolve — the .hky document + the load-order merge + the merged graph
#include "codec/serialization/HkyArchive.h"
#include "interface/UnitSource.h"
#include "decompile/BehaviorDecompile.h"

// schema — the Havok class-schema registry (merge classifier / model interpreter)
#include "interface/reflection/HavokSchema.h"

// anim — decode/compile/export animations (schema-native; the CB pipeline itself)
#include "interface/AnimationDef.h"
#include "codec/format/AnimationYamlLoader.h"
#include "compile/AnimationCompiler.h"
#include "decompile/AnimationDecompiler.h"

// animdata — the animationdata (motion / root-motion) model + its YAML sidecar
#include "interface/AnimationData.h"
#include "codec/format/AnimDataYaml.h"

// skeleton — decode/compile/export skeletons (schema-native codec; anim + ragdoll)
#include "interface/SkeletonData.h"
#include "decompile/SkeletonImport.h"
#include "compile/SkeletonCompiler.h"
#include "codec/format/SkeletonYaml.h"

namespace cb {

// ── cb::resolve ──────────────────────────────────────────────────────────────
namespace resolve {
    using Archive     = CB::engine::codec::HkyArchive;         // a packed .hky bundle, decompressed in memory
    using UnitKind    = CB::engine::codec::HkyArchive::UnitKind;
    using UnitSource  = CB::engine::common::IUnitSource;        // a unit's file view (disk dir OR in-memory .hky)
    using Layer       = CB::engine::common::LayerSource;        // one load-order layer: unit + bundle stem + master table
    using Graph       = CB::engine::decompile::ParsedHky;       // a parsed / merged graph (schema objects + FormIDs)
    using GraphEdit   = CB::engine::decompile::GraphEdit;       // the editing view the compile-time features receive
    using PatchStats  = CB::engine::decompile::HkyPatchStats;

    using CB::engine::decompile::ParseHkyReflective;           // one unit → Graph
    using CB::engine::decompile::MergeLayersReflective;        // layers (base first) → the merged Graph
    using CB::engine::decompile::MergeHkyPatch;                // apply one patch unit onto a Graph
    using CB::engine::decompile::SerializeGraph;               // Graph → packfile bytes
    using CB::engine::decompile::DecompileBehaviorReflective;  // packfile bytes → a unit tree
    using CB::engine::decompile::DeriveHkyPatch;               // (base, mod) packfile bytes → the mod's patch unit
}  // namespace resolve

// ── cb::schema ───────────────────────────────────────────────────────────────
namespace schema {
    using Registry = CB::engine::schema::SchemaRegistry;         // load Havok/ → classify/size/interpret classes
}  // namespace schema

// ── cb::anim ─────────────────────────────────────────────────────────────────
namespace anim {
    using Def             = CB::engine::anim::AnimationDef;         // the authoring model (tracks/floats/annotations)
    using YamlLoader      = CB::engine::anim::AnimationYamlLoader;  // animation.yaml ↔ Def
    using CompileResult   = CB::engine::anim::AnimCompileResult;    // compile → packfile bytes
    using DecompileResult = CB::engine::anim::AnimDecompileResult;  // .hkx bytes → animation.yaml

    using CB::engine::anim::CompileAnimation;        // Def → .hkx bytes (schema-native)
    using CB::engine::anim::CompileAnimationToFile;  // Def → .hkx file
    using CB::engine::anim::DecompileAnimation;      // .hkx bytes → animation.yaml (the export leg)
}  // namespace anim

// ── cb::animdata ───────────────────────────────────────────────────────────────
namespace animdata {
    using MotionRecord = CB::engine::animdata::MotionRecord;   // one clip's root-motion record
    using Project      = CB::engine::animdata::Project;         // an actor's animationdata project
    using CB::engine::animdata::EmitMotionSidecar;   // MotionRecord → motion/<clip>.yaml sidecar
    using CB::engine::animdata::ParseMotionSidecar;  // sidecar text → MotionRecord
    using CB::engine::animdata::EmitMotionYaml;      // project + roster → the combined motion.yaml
    using CB::engine::animdata::ParseMotionYaml;     // motion.yaml text → MotionRecord[]
}  // namespace animdata

// ── cb::skeleton ───────────────────────────────────────────────────────────────
namespace skeleton {
    using Data          = CB::engine::skeleton::SkeletonData;          // the plain skeleton model
    using BoneData      = CB::engine::skeleton::SkeletonBoneData;
    using Physics       = CB::engine::skeleton::BonePhysics;           // authored ragdoll knobs on a bone
    using Bumper        = CB::engine::skeleton::SkeletonBumper;
    using BoneAdd       = CB::engine::skeleton::SkeletonBoneAdd;       // one bone-add layer entry
    using CompileResult = CB::engine::skeleton::SkeletonCompileResult;

    using CB::engine::skeleton::LoadSkeletonsFromHkx;   // skeleton.hkx → SkeletonData (+ ReadSkeletonPhysics)
    using CB::engine::skeleton::ReadSkeletonPhysics;    // attach ragdoll physics onto the anim skeleton
    using CB::engine::skeleton::CompileSkeleton;        // anim skeleton → .hkx
    using CB::engine::skeleton::CompileSkeletonFull;    // + derived ragdoll (6-variant skeleton.hkx)
    using CB::engine::skeleton::CompileSkeletonOverBase;// rebuild anim bones over a base, carry the rest
    using CB::engine::skeleton::CompileSkeletonToFile;
    using CB::engine::skeleton::EmitSkeletonYaml;       // SkeletonData → combined yaml
    using CB::engine::skeleton::EmitSkeletonYamlTree;   // SkeletonData → bonelist.yaml + bones/ unit
    using CB::engine::skeleton::LoadSkeletonYaml;       // dir / combined file → SkeletonData
    using CB::engine::skeleton::LoadSkeletonYamlFromTexts;
    using CB::engine::skeleton::LoadSkeletonLayer;      // bone-add layer dir → additions
    using CB::engine::skeleton::LoadSkeletonLayerFromTexts;
    using CB::engine::skeleton::MergeBoneAdditions;     // append bone-adds onto a base skeleton
}  // namespace skeleton

}  // namespace cb
