#pragma once
// havok-model — the name-keyed .hky model + schema-driven emit/parse (havok-core v2 rewrite, Stage 3).
//
// Sits on top of havok-io's generic SchemaObject graph and projects it to/from the .hky source tree
// (the per-node YAML format BR authors + compiles). The .hky is a SEMANTIC representation, not a raw
// object dump: nodes are identified by a canonical id (the yaml filename), pointers render as id refs,
// enums/events/variables render by NAME, and SERIALIZE_IGNORED fields are dropped.
//
// LAYERING (see CMakeLists): canonical ids are owned by the IDENTITY/INDEX pass here — NOT the Havok/
// schema, NOT the emit formatting. The emit is a pure projection consuming the ids the identity pass
// produced. This mirrors the pipeline's name↔index boundary.
//
// Single public header (monorepo rule): consumers write  #include <havok-model/HavokModel.h>.

#include <havok-io/HavokIo.h>
#include <havok-schema/HavokSchema.h>

#include "havok/classes/IHavokObject.h"

#include <memory>
#include <string>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

namespace havok::model {

// ── identity/index pass ───────────────────────────────────────────────────────
// Walk the SchemaObject graph in ENCOUNTER ORDER (matching the reference BehaviorDecompiler's DFS, so
// the yaml filenames + id refs are byte-stable) and assign each object a canonical id + a category
// folder (clips / states / transitions / generators / selectors / references / tagging / modifiers /
// data). Ids live here; the emit consumes this map.
struct Identity {
    // obj -> canonical id (yaml filename + ref). A CANONICAL STRING: base nodes are the unpadded decimal
    // tagfile number ("184"); a Nemesis new node is its string token ("CASSIE$0"). Numeric ids render
    // byte-identically to a plain integer everywhere, so string ids cost the base path nothing while
    // letting patch-added nodes (and refs to them) live in the same graph.
    std::unordered_map<const IHavokObject*, std::string> ids;
    std::unordered_map<const IHavokObject*, std::string> category;  // obj -> folder
};

// The .hky folder a class's node lives in ("clips", "states", …), or "" if the class is inlined into
// its owner's YAML rather than getting its own file. A pure function of the class name (from the
// reference decompiler's per-class emit sites).
std::string CategoryForClass(const std::string& className);

// Assign canonical ids + categories over a fully-deserialized graph. `des` must have completed a full
// Deserialize (root walk) so ListObjects/RefsInReadOrder/DeserializedObjects are populated.
//
// Ids come from the IDENTITY/INDEX step, which has two providers:
//   • tagfile-aligned (vanilla, matches Skyrim.hky): pass the matching vanilla tagfile `xmlText` — ids
//     are the stable #NNNN recovered by the structural oracle (havok::sct::AlignTagfile), the identity
//     the runtime merges by. This is NOT computable from the packfile alone.
//   • encounter-order fallback (no XML): post-order (ReadCompletionOrder) numbering for a self-
//     consistent hkx→hky→hkx round-trip when Skyrim.hky-exact filenames aren't required.
// Category is a pure function of class either way. (Stage 3, increment 2.)
Identity AssignIdentity(PackFileDeserializer& des, const schema::SchemaRegistry& reg,
                        const std::string& xmlText = {});

// ── schema-driven emit ────────────────────────────────────────────────────────
// Emit the per-node `.hky` files for a behavior graph into `outDir` — `<category>/<id>.yaml` for every
// node with a non-empty category, a pure projection of the SchemaObject graph driven by the schema +
// the Identity map (ids/refs) + name resolution from the graph's hkbBehaviorGraphStringData. Gated
// file-vs-live-`vanbase`. (Stage 3, increment 3 — built out category by category.)
// `deltaIds` (optional): emit ONLY the file nodes a patch touched — the per-mod .hky DELTA LoadMerged
// serves. Each category-"" delta id (an inline sub-object a Nemesis patch edits by its own #NNNN) is
// folded up to the file node that inlines it; an id with no file-node owner is reported via `warnings`
// as moot (not referenced by the merged graph). nullptr = emit the whole graph (the base bundle).
bool EmitHky(const Identity& identity, const schema::SchemaRegistry& reg,
             const std::string& outDir, std::string& err, const std::set<std::string>* deltaIds = nullptr,
             std::vector<std::string>* warnings = nullptr);

// Emit `<outDir>/data/additive.yaml` — the vocabulary a merge ADDED over the base (events/variables/
// character-properties present in the merged graph's string-data but not the base's), each with the
// type/flags/initial-value read from the merged graph-data. The runtime unions this in. No file is
// written when nothing was added. Pairs with EmitHky's delta mode to complete a per-mod .hky delta.
bool EmitAdditiveVocab(const Identity& baseId, const Identity& mergedId,
                       const std::string& outDir, std::string& err);

// ── schema-driven tagfile emit (the tagfile codec — base-self-align) ──────────────────────────────
// Emit the SchemaObject graph as the Havok TAGFILE __data__ section (the `<hkobject name="#NNNN" …>`
// form the converter's structural oracle + bashed merge consume as `vanById`). A pure, per-field
// projection driven by the schema — the data-driven replacement for the converter's per-class
// tagfile populators. Objects are emitted in Identity id order; refs render as `#NNNN`, inline structs
// nest an unnamed `<hkobject>`, SERIALIZE_IGNORED fields render as comments. With encounter-order ids
// (AssignIdentity without a tagfile XML), this reproduces Skyrim.hky's canonical numbering — so the
// base binary IS its own tagfile, and ConvertPatch needs no external template. Gated by re-emitting
// the vanilla templates/*.xml corpus. Returns the __data__ objects (no packfile/section wrapper).
std::string EmitTagfile(const Identity& identity, const schema::SchemaRegistry& reg, std::string& err);

// ── schema-driven tagfile PARSE (the inverse codec — model-merge foundation) ───────────────────────
// Parse a Havok TAGFILE __data__ fragment (a run of `<hkobject name="#NNNN" class="C">` blocks, with or
// without the packfile/section wrapper) into a SchemaObject graph + its Identity — the exact inverse of
// EmitTagfile, driven by the SAME schema field walk. Refs (`#NNNN`) wire by id in a second pass; inline
// structs recurse; enums/flags decode via ResolveEnum; SERIALIZE_IGNORED fields (emitted as comments,
// never hkparams) stay at their Init defaults. This is the reusable primitive under (b) — apply Nemesis
// patches to the Skyrim.hky model BY #NNNN with no binary deserialize/AlignTagfile — and under runtime
// tagfile conversion. Gated by round-trip: ParseTagfile(EmitTagfile(g)) re-emits byte-identical.
//
// NOTE (first cut): the field kinds EmitTagfile does not emit (QsTransform(Array), BoolArray, EmptyArray,
// EmptyPtr) are likewise not parsed — they keep Init defaults, which round-trips against EmitTagfile but
// is a known gap for any consumer that needs those fields verbatim.
struct ParsedTagfile {
    std::vector<std::shared_ptr<io::SchemaObject>> objects;   // owns the graph's lifetime
    Identity                                       identity;  // ids + category over `objects`
};
bool ParseTagfile(const std::string& xmlText, const schema::SchemaRegistry& reg,
                  ParsedTagfile& out, std::string& err);

// ── id-keyed model merge (the core of (b)) ─────────────────────────────────────────────────────────
// Overlay Nemesis patch tagfiles onto a vanilla base tagfile BY #NNNN — the data-driven replacement for
// ConvertPatch's typed deserialize + AlignTagfile + typed populate. The base's ids come straight from its
// tagfile (no binary, no oracle, no off2id); patch ids match by construction. Merge is a field-wise
// overlay in load order (base, then each patch — a later layer wins on the fields it names, keeps the
// rest), and refs resolve across the MERGED id space (a patch node may point at a base-only node). The
// result is a single SchemaObject graph ready for EmitHky (the .hky delta LoadMerged serves) or EmitTagfile.
// `deltaIds` receives every id a patch overrode or added — the set a per-mod .hky delta emits.
//
// Base + patches are already the Nemesis flow's inputs: pass each patch through xml::StripPatchOriginals
// (apply MOD_CODE OPEN edits) before handing its text here.
bool MergeTagfiles(const std::string& baseXml, const std::vector<std::string>& patchXmls,
                   const schema::SchemaRegistry& reg, ParsedTagfile& out,
                   std::vector<std::string>& deltaIds, std::string& err);

// One mod's full behavior delta, data-driven end to end — the schema-path replacement for the converter's
// typed ConvertPatch+DecompileNativeDelta. Reads every `#*.txt` in `patchDirs` (load order), applies each
// Nemesis MOD_CODE edit (StripPatchOriginals), overlays them onto `baseTagfileXml` by #NNNN (MergeTagfiles,
// new #code$N nodes included), and writes the per-mod `.hky` delta into `outDeltaDir` — reachable delta
// file nodes + data/additive.yaml (added vocab). `baseTagfileXml` is the vanilla graph's tagfile (its
// #NNNN are the canonical ids the runtime base is keyed by). Faithful to the patch by construction (same
// parse+emit machinery gated byte-identical to the typed path on overrides, and strictly more complete on
// new nodes, which the typed populators drop).
struct ModDeltaResult {
    bool                     ok = false;
    std::string              error;
    int                      patchNodes = 0;   // #*.txt patch objects read (overrides + new)
    int                      deltaIds   = 0;   // ids a patch overrode or added
    std::vector<std::string> warnings;
};
ModDeltaResult ConvertModDelta(const std::string& baseTagfileXml, const std::vector<std::string>& patchDirs,
                               const schema::SchemaRegistry& reg, const std::string& outDeltaDir);

// ── schema-driven BUILDER (compile direction, Migration M2+) ──────────────────
// Construct a node's generic SchemaObject from its Def — the schema-driven replacement for the typed
// BehaviorBuilder (which builds hk* C++ objects). The result serializes through havok-io, so the compile
// output is the serializer's bytes with NO typed hk* class in the loop. Grown node-by-node, each gated
// byte-identical against the typed compile. First node: hkbClipGenerator (the Animation Relay bind
// target). Pointer sub-nodes (triggers, variableBindingSet) are a later increment — null for now.
struct ClipGeneratorDef;      // fwd (havok/model/defs/GeneratorDefs.h)
struct BlenderGeneratorDef;   // fwd
struct ManualSelectorDef;     // fwd
std::shared_ptr<io::SchemaObject> BuildClip(const ClipGeneratorDef& def, const schema::SchemaRegistry& reg);

// A node that references OTHER nodes (blender children, selector variants, …) takes a resolver that maps
// a referenced node's name to its SchemaObject — the graph-edge abstraction. The full compiler supplies a
// memoized "build-all-nodes" resolver; a per-node gate can point edges at the read graph instead.
using GenResolver = std::function<std::shared_ptr<io::SchemaObject>(const std::string&)>;
std::shared_ptr<io::SchemaObject> BuildBlender(const BlenderGeneratorDef& def, const schema::SchemaRegistry& reg,
                                               const GenResolver& resolve, const std::vector<std::string>& boneNames = {});
std::shared_ptr<io::SchemaObject> BuildSelector(const ManualSelectorDef& def, const schema::SchemaRegistry& reg,
                                                const GenResolver& resolve);

// State-machine family (the biggest node). A state is its own graph node (referenced by its SM via
// the resolver); notify-event arrays / transition arrays / the transition condition are OWNED sub-nodes
// built inline. Post the Stage-4 bindings-resolve pass, event/variable names are pre-resolved to ids —
// the builders set ids/indices directly (no name lookup here).
// Modifier family (batch 1: leaf + edge-only). Edge-carrying ones take the GenResolver.
struct ModifierGeneratorDef;         // fwd (havok/model/defs/ModifierDefs.h)
struct ModifierListDef;              // fwd
struct BSIsActiveModifierDef;        // fwd
struct EventDrivenModifierDef;       // fwd
struct BSEventEveryNEventsModifierDef; // fwd
struct BSInterpValueModifierDef;     // fwd
std::shared_ptr<io::SchemaObject> BuildModifierGenerator(const ModifierGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildModifierList(const ModifierListDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildIsActiveModifier(const BSIsActiveModifierDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildEventDrivenModifier(const EventDrivenModifierDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildEventEveryN(const BSEventEveryNEventsModifierDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildInterpValue(const BSInterpValueModifierDef& def, const schema::SchemaRegistry& reg);

// Data arrays + the modifiers that own them (batch 2). The owning modifier takes its already-built
// array (an owned sub-node) so the array builder stays reusable and the ownership is explicit.
struct ExpressionDataArrayDef;        // fwd (havok/model/defs/ModifierDefs.h)
struct EventRangeDataArrayDef;        // fwd
struct EvaluateExpressionModifierDef; // fwd
struct EventsFromRangeModifierDef;    // fwd
std::shared_ptr<io::SchemaObject> BuildExpressionDataArray(const ExpressionDataArrayDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildEventRangeDataArray(const EventRangeDataArrayDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildEvaluateExpression(const EvaluateExpressionModifierDef& def, const schema::SchemaRegistry& reg,
                                                          const std::shared_ptr<io::SchemaObject>& expressions);
std::shared_ptr<io::SchemaObject> BuildEventsFromRange(const EventsFromRangeModifierDef& def, const schema::SchemaRegistry& reg,
                                                       const std::shared_ptr<io::SchemaObject>& eventRanges);

struct BSOffsetAnimationGeneratorDef; // fwd (havok/model/defs/GeneratorDefs.h)
struct BSSynchronizedClipGeneratorDef; // fwd
struct PoseMatchingGeneratorDef; // fwd
std::shared_ptr<io::SchemaObject> BuildOffsetAnim(const BSOffsetAnimationGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildSynchronizedClip(const BSSynchronizedClipGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildPoseMatching(const PoseMatchingGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve, const std::vector<std::string>& boneNames = {});

// Simple generators + bone-index array (batch 3).
struct BehaviorReferenceGeneratorDef;        // fwd (havok/model/defs/GeneratorDefs.h)
struct BSiStateTaggingGeneratorDef;          // fwd
struct BSCyclicBlendTransitionGeneratorDef;  // fwd
struct ReferencePoseGeneratorDef;            // fwd
struct BGSGamebryoSequenceGeneratorDef;      // fwd
struct BSBoneSwitchGeneratorDef;             // fwd
struct BoneIndexArrayDef;                    // fwd (havok/model/defs/ModifierDefs.h)
std::shared_ptr<io::SchemaObject> BuildBehaviorReference(const BehaviorReferenceGeneratorDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildStateTagging(const BSiStateTaggingGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildCyclicBlend(const BSCyclicBlendTransitionGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildReferencePose(const ReferencePoseGeneratorDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildGamebryoSequence(const BGSGamebryoSequenceGeneratorDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildBoneIndexArray(const BoneIndexArrayDef& def, const schema::SchemaRegistry& reg,
                                                      const std::vector<std::string>& boneNames = {});
std::shared_ptr<io::SchemaObject> BuildBoneSwitch(const BSBoneSwitchGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve, const std::vector<std::string>& boneNames = {});

// Flat generic modifiers (schema-driven field-walk dispatch on className).
struct GenericModifierDef; // fwd (havok/model/defs/ModifierDefs.h)
std::shared_ptr<io::SchemaObject> BuildGenericModifier(const GenericModifierDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve,
                                                      const std::vector<std::string>& boneNames = {});

// Specialized modifiers (foot-IK, iState). Others (ragdoll controls, look-at, keyframe) build via the
// GenericModifierDef path in BuildGenericSpecial below.
struct FootIkControlsModifierDef; // fwd (havok/model/defs/ModifierDefs.h)
struct FootIkModifierDef;         // fwd
struct BSIStateManagerModifierDef; // fwd
std::shared_ptr<io::SchemaObject> BuildFootIkControls(const FootIkControlsModifierDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildFootIkModifier(const FootIkModifierDef& def, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildIStateManager(const BSIStateManagerModifierDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);

// Generic-based specialized modifiers (nested-struct-from-flat-params + inline-object arrays + owned
// bone-index-array). Consume the GenericModifierDef the decompiler produces; the owned bone arrays are
// built by the caller and passed in.
std::shared_ptr<io::SchemaObject> BuildRagdollContactListener(const GenericModifierDef& def, const std::shared_ptr<io::SchemaObject>& bones, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildPoweredRagdoll(const GenericModifierDef& def, const std::shared_ptr<io::SchemaObject>& bones, const std::shared_ptr<io::SchemaObject>& boneWeights, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildRigidBodyRagdoll(const GenericModifierDef& def, const std::shared_ptr<io::SchemaObject>& bones, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildKeyframeBones(const GenericModifierDef& def, const std::shared_ptr<io::SchemaObject>& bonesList, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildLookAt(const GenericModifierDef& def, const schema::SchemaRegistry& reg);
struct BoneWeightsDef; // fwd (havok/model/defs/CommonDefs.h)
std::shared_ptr<io::SchemaObject> BuildBoneWeights(const BoneWeightsDef& bw, const schema::SchemaRegistry& reg, const std::vector<std::string>& boneNames = {});

// Graph-level assembler (the roster + graph container; the prerequisite for a whole-hkx byte gate).
struct BehaviorGraphDataDef;  // fwd (havok/model/defs/BehaviorDef.h)
struct BehaviorDef;           // fwd
std::shared_ptr<io::SchemaObject> BuildGraphData(const BehaviorGraphDataDef& gd, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildBehaviorGraph(const BehaviorDef& beh, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildRootContainer(const std::string& graphKey, const schema::SchemaRegistry& reg, const GenResolver& resolve);

// THE COMPILER ENTRY: assemble a whole behavior graph (hkRootLevelContainer root) from a merged, bindings-
// resolved BehaviorData — the schema-driven replacement for the typed BehaviorBuilder::Build(). Memoized
// build-all (shared nodes built once); the caller serializes the returned root with a packfile header.
struct BehaviorData; // fwd (havok/model/BehaviorData.h)
std::shared_ptr<io::SchemaObject> AssembleGraph(const BehaviorData& data, const schema::SchemaRegistry& reg);

struct ProjectSpec;  // fwd (havok/model/ProjectData.h)
// Schema-driven project emit (hkbProjectData + hkbProjectStringData in a hkRootLevelContainer). The
// data-driven equivalent of havok-core's typed BuildProject; byte-identical, and where BuildProject
// dispatches when the schema compiler is enabled.
std::shared_ptr<io::SchemaObject> AssembleProject(const ProjectSpec& spec, const schema::SchemaRegistry& reg);

struct CharacterData;  // fwd (havok/model/defs/CharacterDefs.h)
// Schema-driven character emit (hkbCharacterData + its owned string-data / value-set / foot-IK /
// mirrored-skeleton / bone-weight objects). The data-driven equivalent of havok-core's typed
// CharacterBuilder / CompileCharacter; byte-identical, and where CompileCharacter dispatches when the
// schema compiler is enabled.
std::shared_ptr<io::SchemaObject> AssembleCharacter(const CharacterData& data, const schema::SchemaRegistry& reg);

struct TransitionEffectDef; // fwd (havok/model/defs/StateMachineDefs.h)
std::shared_ptr<io::SchemaObject> BuildTransitionEffect(const TransitionEffectDef& def, const schema::SchemaRegistry& reg);
struct StateMachineDef;    // fwd (havok/model/defs/StateMachineDefs.h)
struct StateDef;           // fwd
struct TransitionInfoDef;  // fwd
struct EventPropertyDef;   // fwd
std::shared_ptr<io::SchemaObject> BuildEventArray(const std::vector<EventPropertyDef>& events, const schema::SchemaRegistry& reg);
std::shared_ptr<io::SchemaObject> BuildTransitions(const std::vector<TransitionInfoDef>& transitions,
                                                   const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildState(const StateDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);
std::shared_ptr<io::SchemaObject> BuildStateMachine(const StateMachineDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve);

} // namespace havok::model
