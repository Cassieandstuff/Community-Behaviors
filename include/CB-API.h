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
//   cb::setdata  — the animationsetdata (asdsf) model: each project's sets, their gates, attacks and load lists
//   cb::skeleton — read skeletons (schema-native; anim + ragdoll physics summary) from a .hkx or a bundle unit
//   cb::schema   — the SchemaRegistry to interpret a resolved model / classify merges
//   cb::mm       — build a motion-matching bundle (databases + chooser → its behavior unit + roster patches), and
//                  the same database and scoring the game runs (measure clips, build the database, search, top candidates)
//   cb::relay    — the condition language: compile and evaluate an expression over caller-supplied variables
//   cb::locomotion — a character's movement step (GASP's CMC model + the facing spring), the horse ladder, slope math,
//                  the dead blend between poses
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

// setdata — the animationsetdata model (the asdsf .txt and its decomposed setdata/<stem>.yaml folder)
#include "codec/format/AnimationSetData.h"
#include "codec/format/AnimSetDataYaml.h"
#include "interface/MotionSample.h"
#include "interface/GraphTrace.h"

// skeleton — read skeletons (schema-native codec; anim + ragdoll physics summary)
#include "interface/SkeletonData.h"
#include "decompile/SkeletonImport.h"

// mm — the motion-matching bundle generator (the one hky-tool's generators run)
#include "compile/MotionMatchingBundle.h"
#include "compile/MotionMatchingCache.h"
#include "compile/MotionFeatures.h"
#include "locomotion/PoseSearch.h"

// relay — the condition language's game-free core
#include "relay/Relay.h"

// locomotion — the pure movement step the plugin's Advanced Controller runs, game-free
#include "locomotion/Movement.h"
#include "locomotion/Horse.h"
#include "locomotion/Slope.h"
#include "locomotion/Pogo.h"
#include "locomotion/ChooserInputs.h"
#include "locomotion/DeadBlend.h"
#include "locomotion/SpeedProfile.h"
#include "codec/resource/PathKey.h"

namespace cb {

// ── cb::resolve ──────────────────────────────────────────────────────────────
namespace resolve {
    using Archive     = CB::engine::codec::HkyArchive;         // a packed .hky bundle, decompressed in memory
    using UnitKind    = CB::engine::codec::HkyArchive::UnitKind;
    using CB::engine::codec::resource::PathKey;                 // a path's identity: the one place a path folds case
    using CB::engine::codec::resource::ClipSlotKey;             // which animation slot a name binds: the engine keys them by file name
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
    using CB::engine::decompile::ParsePackfile;                ///< packfile bytes → a Graph (SerializeGraph's inverse)
    using CB::engine::decompile::EmitHkyReflective;            ///< Graph::identity → a unit folder (ParseHkyReflective's inverse)
    using CB::engine::decompile::DecompileBehaviorReflective;  // packfile bytes → a unit tree
    using CB::engine::decompile::MergeLayersSkippingBroken;    ///< a load order in use: a mod that can't apply is skipped for that graph
    using CB::engine::decompile::MergeBreak;                   ///< a place the edited layer names a node that's gone
    using CB::engine::decompile::SkippedLayer;                 ///< a mod that couldn't apply to a graph
    using CB::engine::decompile::StatedField;                  ///< a field one layer states for a node
    using CB::engine::decompile::DeriveEditedPatch;            ///< (base, edited) graphs → a patch by FormID, and its masters
    using CB::engine::decompile::DeriveHkyPatch;               // (base, mod) packfile bytes → the mod's patch unit
    using CB::engine::decompile::kUnsavedOwner;                ///< the owner of an editor's new objects until a save names their project
    using CB::engine::decompile::IsNodeClass;                  ///< whether a class's objects get a file of their own

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
    using CB::engine::schema::ScalarWidth;                       ///< a scalar's width in bytes
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
    using CB::engine::anim::SampleCurve;             ///< a curve → its value at a time
    using CB::engine::anim::EmitAnimCurvesYaml;      ///< curves → the unit's `curves:` block
    using CB::engine::anim::EmitAnimationYaml;       ///< Def → the unit's text (CBAnimation, through the schema)

    using CB::engine::anim::CompileAnimation;        // Def → .hkx bytes (schema-native)
    using CB::engine::anim::CompileAnimationToFile;  // Def → .hkx file
    using CB::engine::anim::DecodeAnimation;         // .hkx bytes → Decoded (spline or interleaved)
    using CB::engine::anim::DecompileAnimation;      // .hkx bytes → animation.yaml (the export leg)
    /// @brief `def`'s motion curves from its root motion: Speed, MoveDirection, TurnRate, Acceleration and, with the leg chains, the foot locks.
    /// @return nothing with `err` set when it has no root motion to measure.
    inline std::optional<std::vector<AnimCurveDef>> MeasureCurves(const Def& def, std::string& err) {
        const auto motion = CB::engine::anim::MeasureClip(def, err);
        if (!motion) return std::nullopt;
        return CB::engine::anim::MeasureMotionCurves(*motion, {});
    }
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

// ── cb::setdata ────────────────────────────────────────────────────────────────
/// Skyrim's moveset table: per project, sets gated on variable ranges, each with the attack events it lists and the
/// animations it loads (`animations` = paths, `crcs` = what the engine matches; CompileSetdataCrcs derives one from the other).
namespace setdata {
    using SingleFile    = CB::engine::animsetdata::SingleFile;      ///< every project, in the engine's order
    using Project       = CB::engine::animsetdata::Project;         ///< one project's sets (header "DefaultMaleData\DefaultMale.txt")
    using Set           = CB::engine::animsetdata::SetFile;         ///< a set: gate, equip events, attacks, load list
    using Gate          = CB::engine::animsetdata::TypeCondition;   ///< `variable` in [value, extra]
    using Attack        = CB::engine::animsetdata::Attack;          ///< an attack event -> clip generator names
    using CrcTriple     = CB::engine::codec::crc::CrcTriple;        ///< an animation path's (folder, file, ext) hash
    using ParseError    = CB::engine::animsetdata::ParseError;
    using CB::engine::animsetdata::ReadSingleFile;           ///< the .txt (through its metadata descriptors) -> SingleFile
    using CB::engine::animsetdata::ParseSetdataIndexYaml;    ///< the setdata folder's index.yaml -> project headers, in order
    using CB::engine::animsetdata::ParseSetdataUnitYaml;     ///< setdata/<stem>.yaml -> one Project
    using CB::engine::animsetdata::StemForHeader;            ///< project header -> its setdata/<stem>.yaml stem
    using CB::engine::animsetdata::CompileSetdataCrcs;       ///< a set's `animations` -> its `crcs`
    using CB::engine::codec::crc::TripleForAnimation;        ///< a Data path ("meshes\actors\...\x.hkx") -> its CrcTriple
    using CB::engine::animsetdata::GateValueName;            ///< (variable, value) -> the enum item naming it, "" for a number
}  // namespace setdata

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
    using CB::engine::skeleton::ReadSkeletonLayers;     ///< a skeleton's layers (a complete unit + per-bone patches) → SkeletonData
    using CB::engine::decompile::ReadLayeredSkeletonBones;   ///< a skeleton's layers → its bone names, in served order
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

    using CB::engine::compile::kMmDisableReselection;          ///< MmClip::flags bits
    using CB::engine::compile::kMmLooping;
    using CB::engine::compile::kMmChooserVariables;            ///< the chooser inputs every graph declares, by name

    using CB::engine::compile::MmAnimationName;                ///< clip path → its roster entry
    using CB::engine::compile::BuildMotionMatchingBehavior;    ///< spec → the behavior unit's files
    using CB::engine::compile::BuildMotionMatchingRosters;     ///< spec → one roster patch per character
    using CB::engine::compile::BuildMotionMatchingBundle;      ///< both

    // The database and scoring the game runs. The bundle generator holds the plain names (Clip, Database, ChooserRow),
    // so the search's namesakes take Search*.
    using CachedClip       = CB::engine::anim::MmCachedClip;       ///< one clip's measurement, as the cache stores it
    using SearchClip       = CB::engine::locomotion::Clip;         ///< one child of the selector, as the database holds it
    using SearchDatabase   = CB::engine::locomotion::Database;     ///< the searched rows, chooser and databases
    using SearchChooserRow = CB::engine::locomotion::ChooserRow;
    using PoseDatabase     = CB::engine::locomotion::PoseDatabase; ///< one of its databases: biases and gates
    using Expression       = CB::engine::locomotion::Expression;   ///< a condition expression read from the graph
    using Biases           = CB::engine::locomotion::Biases;       ///< one database's biases, for one frame
    using CurveVariables   = CB::engine::locomotion::CurveVariables; ///< a clip's curves as relay variables
    using Query            = CB::engine::locomotion::Query;        ///< one search's question
    using TrajectorySample = CB::engine::locomotion::TrajectorySample; ///< one of a question's samples
    using QueryTrajectory  = CB::engine::locomotion::QueryTrajectory;  ///< its samples, ahead then back
    namespace features = CB::engine::anim::features;               ///< the feature row's layout and sample times
    using SearchResult     = CB::engine::locomotion::SearchResult; ///< one search's answer
    using Candidate        = CB::engine::locomotion::Candidate;    ///< one row as TopCandidates scored it
    using BuildReport      = CB::engine::locomotion::BuildReport;
    using ClipSource       = CB::engine::locomotion::ClipSource;   ///< animationName → its measurement
    using ClipNeeds        = CB::engine::locomotion::ClipNeeds;    ///< the typed schemas (and rig) a build needs each clip measured under
    using WarnFn           = CB::engine::locomotion::WarnFn;

    using CB::engine::anim::MeasureMmClip;                     ///< an animation unit's text → its CachedClip
    using CB::engine::anim::MmFingerprint;                     ///< a unit's text → CachedClip::fingerprint
    using CB::engine::anim::WriteMmCache;                      ///< CachedClips → the cache packfile
    using CB::engine::anim::ReadMmCache;                       ///< the cache packfile → CachedClips

    // A pose search schema (CBPoseSearchSchema) and its typed features, as data, and the rig they measure through.
    using SchemaSpec           = CB::engine::anim::SchemaSpec;
    using FeatureSpec          = CB::engine::anim::FeatureSpec;
    using FeatureKind          = CB::engine::anim::FeatureKind;
    using TrajectorySampleSpec = CB::engine::anim::TrajectorySampleSpec;
    using Rig                  = CB::engine::anim::Rig;
    using SchemaSpace          = CB::engine::locomotion::SchemaSpace;   ///< one row space of a SearchDatabase
    using CB::engine::anim::SchemaText;                        ///< a schema as canonical text
    using CB::engine::anim::SchemaKey;                         ///< a schema + rig → the key its rows are cached under
    using CB::engine::anim::MeasureSchema;                     ///< an animation measured against a schema
    using CB::engine::locomotion::RigFromSkeleton;             ///< a parsed skeleton unit → its Rig
    using CB::engine::locomotion::SchemaSpecOf;                ///< a parsed CBPoseSearchSchema → its SchemaSpec

    using DatabaseSpec     = CB::engine::locomotion::DatabaseSpec;    ///< what a database is built from, whoever holds the selector
    using SpecChild        = CB::engine::locomotion::SpecChild;
    using SpecDatabase     = CB::engine::locomotion::SpecDatabase;
    using SpecChooserRow   = CB::engine::locomotion::SpecChooserRow;
    using CB::engine::locomotion::SpecOf;                      ///< a parsed CBPoseSearchGenerator → its DatabaseSpec
    using CB::engine::locomotion::RootSelector;                ///< a graph → its CBPoseSearchGenerator, or null
    using CB::engine::locomotion::BuildDatabase;               ///< selector + clips → the SearchDatabase
    using CB::engine::locomotion::Choose;                      ///< the chooser's pick, per database
    using CB::engine::locomotion::EvaluateBiases;              ///< every database's biases for a pass
    using CB::engine::locomotion::IsActive;                    ///< whether a database is in a pick
    using CB::engine::locomotion::FrameOf;                     ///< a clip's frame nearest a time
    using CB::engine::locomotion::Search;                      ///< the search the game runs
    using CB::engine::locomotion::TopCandidates;               ///< the n best rows, with their costs per feature group
    using CB::engine::locomotion::GroupOf;                     ///< a feature dimension → its group
    using CB::engine::locomotion::kGroups;                     ///< how many groups Candidate::groupCost holds
    using CB::engine::locomotion::kWeightGroups;               ///< the parts a database weighs
    using CB::engine::locomotion::kChannels;                   ///< weight groups per trajectory sample: position, facing, velocity, height
    using CB::engine::locomotion::WeightGroupOf;
    using CB::engine::locomotion::WeightGroupName;             ///< a weight group's name ("position+0.2", "feet")
    using CB::engine::locomotion::DefaultWeights;
    using CB::engine::locomotion::ApplyWeights;                ///< (name, weight) pairs over the defaults
    using CB::engine::locomotion::kIgnoreSurrounding;          ///< frames around the playing row that are the playing row
    using CB::engine::locomotion::kForceMoving;                ///< below this request the search gates on standing
    using CB::engine::locomotion::kWalkRun;                    ///< game units/s: a speed band's start, run's
    using CB::engine::locomotion::kRunSprint;                  ///< game units/s: a speed band's start, sprint's

    // What a chooser reads, shared with the game's frame hook.
    using ChooserInputs     = CB::engine::locomotion::ChooserInputs;      ///< one frame's values
    using ChooserVariables  = CB::engine::locomotion::ChooserVariables;   ///< those values as GraphVar reads them
    using ChooserColumn     = CB::engine::locomotion::ChooserColumn;      ///< one input, and what its values mean
    using ChooserColumnKind = CB::engine::locomotion::ChooserColumnKind;
    using CB::engine::locomotion::kChooserColumns;                        ///< every input, in an editor's order
    using CB::engine::locomotion::InitialChooserInputs;
    using CB::engine::locomotion::MovementInputs;                         ///< the inputs a frame's movement decides
    using CB::engine::locomotion::kMovementMode;                          ///< ChooserInputs indices
    using CB::engine::locomotion::kRotationMode;
    using CB::engine::locomotion::kMovementState;
    using CB::engine::locomotion::kGait;
    using CB::engine::locomotion::kIsStarting;
    using CB::engine::locomotion::kIsPivoting;
    using CB::engine::locomotion::kShouldSpinTransition;
    using CB::engine::locomotion::kSpeed2D;
    using CB::engine::locomotion::kStance;
    using CB::engine::locomotion::kJustLandedLight;
    using CB::engine::locomotion::kJustLandedHeavy;
    using CB::engine::locomotion::kShouldTurnInPlace;
    using CB::engine::locomotion::kJustTraversed;
    using CB::engine::locomotion::kDatabaseLOD;
    using CB::engine::locomotion::kTimeToLand;
    using CB::engine::locomotion::kRowStanding;                ///< SearchDatabase::rowGates bits
    using CB::engine::locomotion::kRowMoving;
    using CB::engine::locomotion::kFlagDisableReselection;     ///< SearchClip::flags bits (kMmDisableReselection's values)
    using CB::engine::locomotion::kFlagLooping;

    // One character's matching over time, as the game's node runs it: the Matcher, the node's playback rate and distance
    // matching, and the inputs' memory the frame hook keeps. A simulation drives these exactly as the game does.
    using Matcher      = CB::engine::locomotion::Matcher;        ///< what plays, when a search is due, the reselect ban
    using MatchRequest = CB::engine::locomotion::MatchRequest;   ///< what one update asks of it
    using MatchStep    = CB::engine::locomotion::MatchStep;      ///< what one update did, its search's query included
    using Distances    = CB::engine::locomotion::Distances;      ///< the movement model's distances to its stop and turn
    using CB::engine::locomotion::ScoreRow;                    ///< any row scored against a query, per feature
    using CB::engine::locomotion::NodeRate;                    ///< the node's playback rate while its speed is bound
    using CB::engine::locomotion::DistanceMatchedTime;         ///< the time a Stops or Pivots clip plays at
    using CB::engine::locomotion::InputsInterrupt;             ///< GASP's interrupt on a change of inputs
    using CB::engine::locomotion::TurnIntent;                  ///< the facing a turn in place turns toward
    using CB::engine::locomotion::ShouldTurnInPlace;           ///< ShouldTurnInPlace and its hold
    using CB::engine::locomotion::FutureVelocity;              ///< GASP's future velocity (IsPivoting's)
    using CB::engine::locomotion::PredictStopDistance;         ///< the model's slide to a stop
    using CB::engine::locomotion::PredictPivotDistance;        ///< the model's run-in to a turn
    using CB::engine::locomotion::kSearchInterval;             ///< seconds between searches
    using CB::engine::locomotion::kSwitchFloor;                ///< seconds after a switch before another search
    using CB::engine::locomotion::kMinDwell;                   ///< seconds a pick plays before another replaces it
    using CB::engine::locomotion::kSwitchMargin;               ///< the share of the playing cost a candidate must beat
    using CB::engine::locomotion::kMinRate;                    ///< the node's playback rate range
    using CB::engine::locomotion::kMaxRate;
    using CB::engine::locomotion::kTurnInPlace;                ///< ShouldTurnInPlace's angle
    inline constexpr int kFeatureDims = CB::engine::anim::features::kDims;   ///< floats per SearchDatabase row
}  // namespace mm

// ── cb::relay ────────────────────────────────────────────────────────────────
/// Animation Relay's game-free core: an expression compiled once and evaluated per pass over caller-supplied variables.
/// RegisterCoreBuiltins gives GraphVar alone, so an expression calling a game-state function does not compile here; Check
/// typechecks one against the catalog instead, which declares every function the game binds.
namespace relay {
    using Value          = CB::engine::relay::Value;           ///< Bool, Int or Float; read with AsBool / AsFloat / AsInt
    using VType          = CB::engine::relay::VType;
    using EvalContext    = CB::engine::relay::EvalContext;     ///< one pass: Reset(nullptr), set `variables`, evaluate
    using CompiledExpr   = CB::engine::relay::CompiledExpr;
    using VariableSource = CB::engine::relay::VariableSource;  ///< what GraphVar reads, by exact name
    using WarnSink       = CB::engine::relay::WarnSink;

    using CB::engine::relay::Compile;                          ///< expression text → CompiledExpr
    using CB::engine::relay::RegisterCoreBuiltins;             ///< register GraphVar; before the first Compile
    using CB::engine::relay::Intern;                           ///< the process-lifetime copy of a string, byte-exact
    using CB::engine::relay::SetWarnSink;                      ///< where the registry's warnings go

    using SyntaxNode   = CB::engine::relay::SyntaxNode;        ///< an expression as written: spellings byte-exact
    using ParseResult  = CB::engine::relay::ParseResult;       ///< the tree, or why it does not parse and where
    using CallableInfo = CB::engine::relay::CallableInfo;      ///< a catalog function: name, returns, params, category, doc
    using ParamInfo    = CB::engine::relay::ParamInfo;         ///< an argument: name, type, the choice table it takes
    using Choice       = CB::engine::relay::Choice;            ///< one named value of a choice table

    using CB::engine::relay::Parse;                            ///< text → syntax tree, resolving no name
    using CB::engine::relay::Print;                            ///< syntax tree → text that parses back to it
    using CB::engine::relay::Check;                            ///< parses and typechecks against the catalog, bound or not
    using CB::engine::relay::ListCallables;                    ///< every function the grammar can call
    using CB::engine::relay::FindCallable;                     ///< one of them by exact name; null when absent
    using CB::engine::relay::Choices;                          ///< a choice table by name ("Hand", "WEAPON_TYPE")
}  // namespace relay

// ── cb::trace ──────────────────────────────────────────────────────────────────
/// One actor's behavior-graph path per update, as data (a runtime's collector fills it, a debugger draws it), and where
/// each serialized object came from, so a traced packfile object resolves to its unit FormID.
namespace trace {
    using NodeRef      = CB::engine::trace::NodeRef;        ///< (loaded file, data-section offset)
    using Visit        = CB::engine::trace::Visit;          ///< a wire that carried the update, with its cumulative weight
    using State        = CB::engine::trace::State;          ///< a state machine's current state
    using Transition   = CB::engine::trace::Transition;     ///< a transition in progress, with its progress
    using Event        = CB::engine::trace::Event;          ///< an event raised, with its sender
    using Frame        = CB::engine::trace::Frame;          ///< one update's path
    using ObjectOrigin = CB::engine::trace::ObjectOrigin;   ///< a serialized object's offset and FormID
}  // namespace trace

// ── cb::locomotion ─────────────────────────────────────────────────────────────
namespace locomotion {
    using Gait         = CB::engine::locomotion::Gait;          ///< None / Walk / Run / Sprint
    using Mode         = CB::engine::locomotion::Mode;          ///< Orient (face the movement) / Strafe (face a target)
    using Profile      = CB::engine::locomotion::Profile;       ///< a character's movement numbers
    using Caps         = CB::engine::locomotion::Caps;          ///< the speed caps a step works under
    using Input        = CB::engine::locomotion::Input;         ///< what a step is asked (stick, gait, target, airborne)
    using State        = CB::engine::locomotion::State;         ///< velocity + facing, carried step to step
    using HorseProfile = CB::engine::locomotion::HorseProfile;  ///< the ridden horse's numbers + its ladder thresholds
    using Rung         = CB::engine::locomotion::Rung;          ///< Walk / Trot / Canter / Gallop
    using Surface      = CB::engine::locomotion::Surface;       ///< the ground under the feet (Havok units)
    using Tuning       = CB::engine::locomotion::Tuning;        ///< settings.ini [Character] numbers the slope math reads
    using Bone         = CB::engine::locomotion::Bone;          ///< one local-pose bone, laid out as hkQsTransform
    using DeadBlend    = CB::engine::locomotion::DeadBlend;     ///< one character's dead-blend state; Step blends a pose
    using DeadBlendTuning = CB::engine::locomotion::DeadBlendTuning;  ///< blend time + coast decay halflife
    using BlendFault   = CB::engine::locomotion::BlendFault;    ///< the first broken blended bone a Step fell back on

    using CB::engine::locomotion::kPlayer;                 ///< the player's Profile
    using CB::engine::locomotion::kHorse;                  ///< the ridden horse's HorseProfile
    using CB::engine::locomotion::kSlopeSpeedHalflife;     ///< the smoothing halflife a caller puts on SlopeSpeedScale

    using CB::engine::locomotion::Step;                    ///< one movement step (velocity, then facing)
    using CB::engine::locomotion::MaxSpeed;                ///< the cap moving along a heading while facing another
    using CB::engine::locomotion::StrafeSpeedMap;          ///< velocity-vs-facing angle → 0 forward, 1 side, 2 back
    using CB::engine::locomotion::SprintDirectionAllowed;  ///< whether the direction allows a sprint
    using CB::engine::locomotion::kStickDeadzone;          ///< below this stick length no direction is asked
    using CB::engine::locomotion::kNoDirection;            ///< Face: leave the frame's move direction as it is
    using CB::engine::locomotion::StickHeadingOf;          ///< stick + camera yaw -> the world heading asked for
    using CB::engine::locomotion::Face;                    ///< a frame's turn rate and move direction toward a facing
    using CB::engine::locomotion::TopRung;                 ///< rider's toggles + deflection → the highest rung
    using CB::engine::locomotion::RungSpeed;               ///< a rung's speed from walk + run speeds
    using CB::engine::locomotion::WrapPi;
    using CB::engine::locomotion::SpringDamperExact;       ///< exact critically damped spring
    using CB::engine::locomotion::GroundVelocityStep;      ///< the body's velocity following the model's
    using CB::engine::locomotion::SlopeRise;
    using CB::engine::locomotion::SlopeSpeedScale;
    using CB::engine::locomotion::SlideStep;
    using CB::engine::locomotion::ClipUphill;
    using CB::engine::locomotion::JumpLaunch;
    using CB::engine::locomotion::FiniteBone;              ///< whether a bone's translation and rotation are finite
    using StickThresholds = CB::engine::locomotion::StickThresholds;   ///< the stick lengths that move, and run
    using CB::engine::locomotion::StickGait;               ///< a running player's light stick walks
    using CB::engine::locomotion::ApplyStick;              ///< the stick on the model's input (turn in place under the move threshold)
    using SpeedProfile    = CB::engine::locomotion::SpeedProfile;      ///< a movement type's walk and run speeds
    using CB::engine::locomotion::DefaultSpeedProfile;     ///< the built-in speeds the controller moves at (GASP's loops)
}  // namespace locomotion

}  // namespace cb
