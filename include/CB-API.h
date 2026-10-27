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
//   cb::resolve  — read .hky bundles + merge a load order → the merged graph, derive a mod's
//                  patch (identical to what the CB compiler does; single owner, no drift), and
//                  write a parsed unit back out (an editor's save)
//   cb::anim     — decode / compile / EXPORT animations exactly as CB does
//   cb::animdata — the animationdata (motion / root-motion) model + its YAML sidecar
//   cb::skeleton — read skeletons (schema-native; anim + ragdoll physics summary) from a .hkx or a bundle unit
//   cb::schema   — the SchemaRegistry to interpret a resolved model / classify merges
//   cb::mm       — build a motion-matching bundle (databases + chooser → its behavior unit + roster patches)
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
#include "codec/serialization/HavokIo.h"
#include "interface/UnitSource.h"
#include "decompile/BehaviorDecompile.h"
#include "decompile/EmbeddedFile.h"
#include "merge/LoadOrderDocument.h"
#include "merge/HkyOverlay.h"
#include "interface/MasterTable.h"
#include "compile/BundleAssembly.h"
#include <vfs/Vfs.h>                        // the Data folder LoadOrderDocument reads (VFS, a PUBLIC dependency)

// schema — the Havok class-schema registry (merge classifier / model interpreter)
#include "interface/reflection/HavokSchema.h"
#include "interface/HavokEnums.h"

// anim — decode/compile/export animations (schema-native; the CB pipeline itself)
#include "interface/AnimationDef.h"
#include "codec/format/AnimationYamlLoader.h"
#include "compile/AnimationCompiler.h"
#include "decompile/AnimationDecompiler.h"

// animdata — the animationdata (motion / root-motion) model + its YAML sidecar
#include "interface/AnimationData.h"
#include "codec/format/AnimDataYaml.h"
#include "interface/MotionSample.h"

// skeleton — read skeletons (schema-native codec; anim + ragdoll physics summary)
#include "interface/SkeletonData.h"
#include "decompile/SkeletonImport.h"

// mm — the motion-matching bundle generator (the one hky-tool's generators run)
#include "compile/MotionMatchingBundle.h"

namespace cb {

// ── cb::resolve ──────────────────────────────────────────────────────────────
namespace resolve {
    using Archive     = CB::engine::codec::HkyArchive;         // a packed .hky bundle, decompressed in memory
    using UnitKind    = CB::engine::codec::HkyArchive::UnitKind;
    using UnitSource  = CB::engine::common::IUnitSource;        // a unit's file view (disk dir OR in-memory .hky)
    using DiskUnitSource = CB::engine::common::DiskUnitSource;  ///< a unit folder on disk, as a UnitSource
    using Layer       = CB::engine::common::LayerSource;        // one load-order layer: unit + bundle stem + master table
    using Graph       = CB::engine::decompile::ParsedHky;       // a parsed / merged graph (schema objects + FormIDs)
    using Identity    = CB::engine::decompile::Identity;        ///< a graph's file-node FormIDs, their folders, minted roster keys
    using GraphEdit   = CB::engine::decompile::GraphEdit;       // the editing view the compile-time features receive
    using PatchStats  = CB::engine::decompile::HkyPatchStats;
    using Object      = CB::engine::codec::io::SchemaObject;    // one graph object: typed field reads by schema name
    using FieldValue  = CB::engine::codec::io::FieldValue;      // a field's raw store (vec4 / qstransform bytes, …)
    using CharacterStrings = CB::engine::decompile::CharacterStrings;  // a character unit's name / rig / behavior

    using CB::engine::decompile::ParseHkyReflective;           // one unit → Graph
    using CB::engine::codec::io::AsSchema;                     // graph pointer → Object (or null)
    using CB::engine::codec::io::FindVariant;                  // root container → the named variant's Object
    using CB::engine::decompile::ReadCharacterStrings;         // character unit → its strings, no rig needed
    using CB::engine::decompile::MergeLayersReflective;        // layers (base first) → the merged Graph
    using CB::engine::decompile::MergeHkyPatch;                // apply one patch unit onto a Graph
    using CB::engine::decompile::SerializeGraph;               // Graph → packfile bytes
    using CB::engine::decompile::EmitHkyReflective;            ///< Graph::identity → a unit folder (ParseHkyReflective's inverse)
    using CB::engine::decompile::DecompileBehaviorReflective;  // packfile bytes → a unit tree
    using CB::engine::decompile::DeriveHkyPatch;               // (base, mod) packfile bytes → the mod's patch unit

    using EmbeddedKind = CB::engine::schema::EmbeddedKind;      ///< a hand-parsed file kind whose root map holds a schema class
    using CB::engine::decompile::EmbeddedKindOf;               ///< file text → its embedded kind (by root key), or null
    using CB::engine::decompile::ParseEmbeddedFile;            ///< file text → the kind's class Object
    using CB::engine::decompile::SpliceEmbeddedFile;           ///< (file text, Object) → the text with those fields rewritten
    using CB::engine::decompile::ParseEmbeddedObject;          ///< a root map → the class Object
    using CB::engine::decompile::EmitEmbeddedObject;           ///< Object → its fields as YAML keys
    using CB::engine::decompile::SampleCurve;                  ///< a `view: curve` Object → its value at a time

    using LoadOrderDocument = CB::engine::merge::LoadOrderDocument;   ///< the resolved .hky load order over a VFS Data folder
    using HkyOverlay = CB::engine::merge::HkyOverlay;   ///< the load order (or the open project) streamed over VFS as Data files
    using CB::engine::codec::formid::BuildMasterTable;          ///< a bundle's ordered master table ([self, skyrim, masters…])
    using BundleInput    = CB::engine::compile::BundleInput;      ///< partitions, loose files, generator and inject configs
    using AssembleReport = CB::engine::compile::AssembleReport;
    using CB::engine::compile::AssembleBundle;                 ///< BundleInput -> a packed .hky (hky-tool assemble, an editor's Export)
}  // namespace resolve

// ── cb::schema ───────────────────────────────────────────────────────────────
namespace schema {
    using Registry = CB::engine::schema::SchemaRegistry;         // load Havok/ → classify/size/interpret classes
    using Field    = CB::engine::schema::Field;                  ///< one member's description (Object::Fields()): kind, refs, unitRef

    // The process-wide registry the anim + skeleton codecs read. Point it at the deployed tree
    // (Data/community_behaviors/schema) before the first decode; it loads once and enforces the version stamp.
    using CB::engine::schema::SetSharedSchemaDir;
    using CB::engine::schema::SharedSchemaDir;
    using CB::engine::schema::SharedRegistry;
    using CB::engine::schema::SharedRegistryError;

    // Enum value tables by enum name (a field's schema `enum:`), e.g. EnumTable("PlaybackMode").at("MODE_LOOPING").
    // Case-sensitive Havok data; unknown enum names return an empty table.
    using CB::engine::common::enums::EnumTable;
}  // namespace schema

// ── cb::anim ─────────────────────────────────────────────────────────────────
namespace anim {
    using Def             = CB::engine::anim::AnimationDef;         // the authoring model (tracks/floats/annotations)
    using YamlLoader      = CB::engine::anim::AnimationYamlLoader;  // animation.yaml ↔ Def
    using CompileResult   = CB::engine::anim::AnimCompileResult;    // compile → packfile bytes
    using DecompileResult = CB::engine::anim::AnimDecompileResult;  // .hkx bytes → animation.yaml
    using Decoded         = CB::engine::anim::DecodedAnimation;     // .hkx bytes → in-memory poses + annotations
    using Pose            = CB::engine::codec::spline::DecodedPose; // one track's local T / R (xyzw) / S at a frame

    // The Def model's parts, under their engine names (a tool fills these to author a clip).
    using CB::engine::anim::Vec3Keyframe;
    using CB::engine::anim::QuatKeyframe;
    using CB::engine::anim::FloatKeyframe;
    using CB::engine::anim::AnimTrackDef;
    using CB::engine::anim::AnimFloatTrackDef;
    using CB::engine::anim::AnimAnnotation;
    using CB::engine::anim::AnimAnnotationTrackDef;
    using CB::engine::anim::AnimCompressionParams;
    using CB::engine::anim::AnimCurveKey;            ///< one curve key (time, value, interpolation)
    using CB::engine::anim::AnimCurveDef;            ///< a named curve over the clip
    using CB::engine::anim::EmitAnimCurvesYaml;      ///< curves → the unit's `curves:` block

    using CB::engine::anim::CompileAnimation;        // Def → .hkx bytes (schema-native)
    using CB::engine::anim::CompileAnimationToFile;  // Def → .hkx file
    using CB::engine::anim::DecodeAnimation;         // .hkx bytes → Decoded (spline or interleaved)
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
    using CB::engine::animdata::LabelSample;         // a verbatim motion sample → its axis-labeled YAML form
    using CB::engine::animdata::UnlabelSample;       // axis-labeled sample → verbatim (also reads the bare form)
}  // namespace animdata

// ── cb::skeleton ───────────────────────────────────────────────────────────────
namespace skeleton {
    using Data          = CB::engine::skeleton::SkeletonData;          // the plain skeleton model
    using BoneData      = CB::engine::skeleton::SkeletonBoneData;
    using Physics       = CB::engine::skeleton::BonePhysics;           // authored ragdoll knobs on a bone
    using Joint         = CB::engine::skeleton::BoneJoint;             // a ragdoll bone's constraint to its parent
    using Bumper        = CB::engine::skeleton::SkeletonBumper;

    using CB::engine::skeleton::LoadSkeletonsFromHkx;   // skeleton.hkx → SkeletonData (+ ReadSkeletonPhysics)
    using CB::engine::skeleton::ReadSkeletonPhysics;    // attach ragdoll physics onto the anim skeleton
    using CB::engine::skeleton::ReadSkeletonUnit;       // a bundle's skeleton unit → SkeletonData, physics attached
    using CB::engine::decompile::ReadSkeletonBones;     // a skeleton unit's bone names, in served order (no parse)
}  // namespace skeleton

// ── cb::mm ───────────────────────────────────────────────────────────────────────
namespace mm {
    using Clip         = CB::engine::compile::MmClip;          ///< one clip of a database → one clip generator child
    using Database     = CB::engine::compile::MmDatabase;      ///< a dance card: a contiguous child range + cost biases
    using ChooserRow   = CB::engine::compile::MmChooserRow;    ///< a `when` expression → the databases searched
    using Variable     = CB::engine::compile::MmVariable;      ///< an extra graph variable declaration
    using VariableDecl = CB::engine::compile::MmVariableDecl;
    using BundleSpec   = CB::engine::compile::MmBundleSpec;    ///< the whole bundle's input
    using Files        = CB::engine::compile::MmFiles;         ///< bundle path → file text

    using CB::engine::compile::kMmDisableReselection;          ///< childFlags bits
    using CB::engine::compile::kMmLooping;
    using CB::engine::compile::kMmChooserVariables;            ///< the chooser inputs every graph declares, by name

    using CB::engine::compile::MmAnimationName;                ///< clip path → its roster entry
    using CB::engine::compile::BuildMotionMatchingBehavior;    ///< spec → the behavior unit's files
    using CB::engine::compile::BuildMotionMatchingRosters;     ///< spec → one roster patch per character
    using CB::engine::compile::BuildMotionMatchingBundle;      ///< both
}  // namespace mm

}  // namespace cb
