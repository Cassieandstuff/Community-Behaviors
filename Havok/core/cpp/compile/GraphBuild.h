// GraphBuild.h — the schema-driven BUILDER (compile direction) half of the former havok-model facade.
// Split out of <havok-model/HavokModel.h> in org-pass firesale phase 3f. Impl: compile/SchemaBuilder.cpp
// (+ ResolveBehaviorBindings in compile/ResolveBindings.cpp).
#pragma once
#include <codec/serialization/HavokIo.h>
#include <interface/reflection/HavokSchema.h>
#include <codec/serialization/packfile/IHavokObject.h>

#include <memory>
#include <string>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

namespace havok::model {
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

// The bindings-resolve PREP pass (event/variable/character-property NAME -> roster index), run on a
// mutable BehaviorData BEFORE AssembleGraph (or the typed BehaviorBuilder). Behavior-preserving; the
// name->index map is context-free so it lifts out as a discrete pass. Impl: havok/model/ResolveBindings.cpp
// (extracted out of havok-core's BehaviorBuilder.cpp so the schema compile path reaches it havok-core-free).
void ResolveBehaviorBindings(BehaviorData& data);

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
