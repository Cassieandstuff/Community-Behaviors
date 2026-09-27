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
//   cb::animdata — the animationdata (motion / root-motion) model + its YAML sidecar
//   cb::skeleton — decode / compile / EXPORT skeletons (schema-native; anim + ragdoll)
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
    using Archive       = CB::core::codec::HkyArchive;         // a packed .hky bundle, decompressed in memory
    using UnitKind      = CB::core::codec::HkyArchive::UnitKind;
    using UnitSource    = CB::core::common::IUnitSource;        // a unit's file view (disk dir OR in-memory .hky)
    using LoadOrder     = CB::core::codec::YamlBehaviorLoader; // LoadMerged — the load-order merge
    using ResolvedGraph = CB::core::common::BehaviorData;       // the resolved, merged graph model
}  // namespace resolve

// ── cb::schema ───────────────────────────────────────────────────────────────
namespace schema {
    using Registry = CB::core::schema::SchemaRegistry;         // load Havok/ → classify/size/interpret classes
}  // namespace schema

// ── cb::anim ─────────────────────────────────────────────────────────────────
namespace anim {
    using Def             = CB::core::anim::AnimationDef;         // the authoring model (tracks/floats/annotations)
    using YamlLoader      = CB::core::anim::AnimationYamlLoader;  // animation.yaml ↔ Def
    using CompileResult   = CB::core::anim::AnimCompileResult;    // compile → packfile bytes
    using DecompileResult = CB::core::anim::AnimDecompileResult;  // .hkx bytes → animation.yaml

    using CB::core::anim::CompileAnimation;        // Def → .hkx bytes (schema-native)
    using CB::core::anim::CompileAnimationToFile;  // Def → .hkx file
    using CB::core::anim::DecompileAnimation;      // .hkx bytes → animation.yaml (the export leg)
}  // namespace anim

// ── cb::animdata ───────────────────────────────────────────────────────────────
namespace animdata {
    using MotionRecord = CB::core::animdata::MotionRecord;   // one clip's root-motion record
    using Project      = CB::core::animdata::Project;         // an actor's animationdata project
    using CB::core::animdata::EmitMotionSidecar;   // MotionRecord → motion/<clip>.yaml sidecar
    using CB::core::animdata::ParseMotionSidecar;  // sidecar text → MotionRecord
    using CB::core::animdata::EmitMotionYaml;      // project + roster → the combined motion.yaml
    using CB::core::animdata::ParseMotionYaml;     // motion.yaml text → MotionRecord[]
}  // namespace animdata

// ── cb::skeleton ───────────────────────────────────────────────────────────────
namespace skeleton {
    using Data          = CB::core::skeleton::SkeletonData;          // the plain skeleton model
    using BoneData      = CB::core::skeleton::SkeletonBoneData;
    using Physics       = CB::core::skeleton::BonePhysics;           // authored ragdoll knobs on a bone
    using Bumper        = CB::core::skeleton::SkeletonBumper;
    using BoneAdd       = CB::core::skeleton::SkeletonBoneAdd;       // one bone-add layer entry
    using CompileResult = CB::core::skeleton::SkeletonCompileResult;

    using CB::core::skeleton::LoadSkeletonsFromHkx;   // skeleton.hkx → SkeletonData (+ ReadSkeletonPhysics)
    using CB::core::skeleton::ReadSkeletonPhysics;    // attach ragdoll physics onto the anim skeleton
    using CB::core::skeleton::CompileSkeleton;        // anim skeleton → .hkx
    using CB::core::skeleton::CompileSkeletonFull;    // + derived ragdoll (6-variant skeleton.hkx)
    using CB::core::skeleton::CompileSkeletonOverBase;// rebuild anim bones over a base, carry the rest
    using CB::core::skeleton::CompileSkeletonToFile;
    using CB::core::skeleton::EmitSkeletonYaml;       // SkeletonData → combined yaml
    using CB::core::skeleton::EmitSkeletonYamlTree;   // SkeletonData → bonelist.yaml + bones/ unit
    using CB::core::skeleton::LoadSkeletonYaml;       // dir / combined file → SkeletonData
    using CB::core::skeleton::LoadSkeletonYamlFromTexts;
    using CB::core::skeleton::LoadSkeletonLayer;      // bone-add layer dir → additions
    using CB::core::skeleton::LoadSkeletonLayerFromTexts;
    using CB::core::skeleton::MergeBoneAdditions;     // append bone-adds onto a base skeleton
}  // namespace skeleton

}  // namespace cb
