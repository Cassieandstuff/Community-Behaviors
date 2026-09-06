#pragma once
#include "havok/classes/Classes.h"
#include "havok/classes/gen/ClassesGen.h"
#include "havok/model/BehaviorData.h"

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

// BehaviorBuilder (Tier B) — turns a loaded BehaviorData (*Def content model) into
// the Tier-A havok-core object graph: hkRootLevelContainer -> hkbBehaviorGraph
// (+ data/stringData) -> rootGenerator state machine -> states -> clips/blenders/
// modifiers/transitions/etc.
//
// This retargets HKBuild\src\BehaviorXmlEmitter.cs: every "emit XML node for X
// with fields …" becomes "construct havok-core object X and set its m_* fields …".
// Name references (state→generator, SM→states, transitions→events/transition
// effects, bindings→variables) are resolved the way the emitter resolves them.
//
// Differences from the XML emitter, by design:
//   - No string ID allocation / two-pass BFS+DFS. C++ resolves references through
//     shared_ptr and a name→object memo map; each node is built at most once and
//     reused, which reproduces the emitter's "visit once, reference by ID" graph
//     shape without the textual indirection.
//   - Enum strings are converted to the numeric field values via HavokEnums.h
//     (the XML compiler did this conversion downstream of the emitter).
//
// Pure C++ — NO YAML/ryml. Parses float-strings via std::stof.

namespace havok::model {

class BehaviorBuilder {
public:
    explicit BehaviorBuilder(const BehaviorData& data) : _data(data) {
        // Stage 4b: name->index resolution moved OUT of the builder into
        // ResolveBehaviorBindings (run before construction). The builder no longer holds
        // name maps and cannot do name lookups — it reads pre-resolved indices. The
        // roster COUNTS are kept, for the numeric-index OOB guards (BR-16, BR-31) that stay here.
        if (data.graphData) {
            _variableCount = static_cast<int>(data.graphData->variables.size());
            _eventCount    = static_cast<int>(data.graphData->events.size());
            _charPropCount = static_cast<int>(data.graphData->characterPropertyNames.size());
        }
    }

    // Build the full graph and return the packfile root.
    std::shared_ptr<hkRootLevelContainer> Build();

    // Build a single node by authored name and return it (or null for
    // unknown/stubbed kinds). Exposes the builder's resolution path for callers
    // that want to construct a subtree without a full behavior root — used by the
    // unit test to exercise leaf builders (e.g. the generic hkbTwistModifier path)
    // that have no generator slot to hang off the behavior graph in the M1 set.
    std::shared_ptr<hkbNode> BuildNodeByName(const std::string& name) { return buildNode(name); }

private:
    const BehaviorData& _data;

    // Roster counts only (for the numeric-index OOB guards); no name maps — resolution
    // happens in ResolveBehaviorBindings before the builder runs (Stage 4b boundary).
    int _variableCount = 0;
    int _eventCount    = 0;
    int _charPropCount = 0;

    // Built graph data objects (shared by hkbBehaviorGraph).
    std::shared_ptr<hkbBehaviorGraphData> _graphData;

    // Memo: node name -> built generator/modifier object (build once, reference
    // many). Keyed by the authored node name (unique across the behavior).
    std::map<std::string, std::shared_ptr<hkbNode>> _nodeMemo;
    // Memo: payload data string -> shared hkbStringEventPayload (dedup, mirrors
    // the emitter's payload deduplication).
    std::map<std::string, std::shared_ptr<hkbStringEventPayload>> _payloadMemo;
    // Memo: transition-effect name -> built effect (shared across transitions).
    std::map<std::string, std::shared_ptr<hkbTransitionEffect>> _effectMemo;
    // Memo: state name -> built StateInfo (shared across state machines). Pandora's
    // merged graphs share ONE StateInfo object across multiple SMs' state lists (e.g.
    // BFCO's Bfco_AttackPower123_* combo states live in both the main and the "Temp"
    // SM); without this the builder rebuilds a fresh copy per reference, splitting the
    // shared state in two and breaking attack chaining / root motion (in-game slide).
    // Safe because the decompiler now emits globally-unique node names, so a repeated
    // name is genuinely the same shared object.
    std::map<std::string, std::shared_ptr<hkbStateMachineStateInfo>> _stateMemo;
    // Identical transition-condition expressions share one object (matches C#).
    std::map<std::string, std::shared_ptr<hkbExpressionCondition>> _conditionMemo;

    // Index accessors (Stage 4b): names are already resolved + cleared by
    // ResolveBehaviorBindings, so these return the pre-resolved `fallback` index and do
    // NO name lookup (there is no name map). The variable accessor keeps the numeric-index
    // OOB guard (BR-16). `name` is expected empty; it is accepted only so the ~35 call
    // sites stay untouched, and is asserted-empty in debug.
    int  resolveEventId(const std::optional<std::string>& name, int fallback) const;
    int  resolveVariableIndex(const std::optional<std::string>& name, int fallback) const;
    int  resolveCharPropIndex(const std::optional<std::string>& name, int fallback) const;
    std::shared_ptr<hkbStringEventPayload> resolvePayload(const std::string& payload);

    // Graph data.
    std::shared_ptr<hkbBehaviorGraphData> buildGraphData();

    // Generator/modifier dispatch — builds (or returns memoized) the node named
    // `name`. Returns null for "null"/empty/unknown.
    std::shared_ptr<hkbNode> buildNode(const std::string& name);

    // Per-kind builders.
    //
    // Coverage note: the Tier-A class set now covers the full closure
    // trueflight.hkx uses, including the previously-stubbed kinds (the transpiler
    // expansion added them under havok/classes/gen/). Each kind below has a
    // concrete Tier-A type and a dedicated builder; the generic-modifier path
    // additionally builds hkbTwistModifier (the one generic modifier the
    // trueflight tree uses) as its concrete type.
    std::shared_ptr<hkbClipGenerator>                  buildClip(const ClipGeneratorDef&);
    std::shared_ptr<hkbBlenderGenerator>               buildBlender(const BlenderGeneratorDef&);
    std::shared_ptr<hkbManualSelectorGenerator>        buildSelector(const ManualSelectorDef&);
    std::shared_ptr<hkbStateMachine>                   buildStateMachine(const StateMachineDef&);
    std::shared_ptr<hkbStateMachineStateInfo>          buildState(const std::string& id, const StateDef&);
    std::shared_ptr<BSiStateTaggingGenerator>          buildStateTagging(const BSiStateTaggingGeneratorDef&);
    std::shared_ptr<hkbBehaviorReferenceGenerator>     buildBehaviorReference(const BehaviorReferenceGeneratorDef&);
    std::shared_ptr<BGSGamebryoSequenceGenerator>      buildGamebryoSequence(const BGSGamebryoSequenceGeneratorDef&);
    std::shared_ptr<BSCyclicBlendTransitionGenerator>  buildCyclicBlend(const BSCyclicBlendTransitionGeneratorDef&);
    std::shared_ptr<BSBoneSwitchGenerator>             buildBoneSwitch(const BSBoneSwitchGeneratorDef&);
    std::shared_ptr<hkbNode>                           buildGenericModifier(const std::string& id, const GenericModifierDef&);

    // Tier-B kinds added with the transpiler class expansion.
    std::shared_ptr<hkbModifierGenerator>              buildModifierGenerator(const ModifierGeneratorDef&);
    std::shared_ptr<BSOffsetAnimationGenerator>        buildOffsetAnim(const BSOffsetAnimationGeneratorDef&);
    std::shared_ptr<hkbModifierList>                   buildModifierList(const ModifierListDef&);
    std::shared_ptr<BSIsActiveModifier>                buildIsActiveModifier(const BSIsActiveModifierDef&);
    std::shared_ptr<BSSynchronizedClipGenerator>       buildSynchronizedClip(const BSSynchronizedClipGeneratorDef&);
    std::shared_ptr<hkbPoseMatchingGenerator>          buildPoseMatching(const PoseMatchingGeneratorDef&);
    std::shared_ptr<hkbEventDrivenModifier>            buildEventDrivenModifier(const EventDrivenModifierDef&);
    std::shared_ptr<BSEventEveryNEventsModifier>       buildEventEveryN(const BSEventEveryNEventsModifierDef&);
    std::shared_ptr<hkbEvaluateExpressionModifier>     buildEvaluateExpression(const std::string& id, const EvaluateExpressionModifierDef&);
    std::shared_ptr<BSInterpValueModifier>             buildInterpValue(const BSInterpValueModifierDef&);
    std::shared_ptr<hkbEventsFromRangeModifier>        buildEventsFromRange(const std::string& id, const EventsFromRangeModifierDef&);
    std::shared_ptr<hkbEventRangeDataArray>            buildEventRangeDataArray(const EventRangeDataArrayDef&);
    std::shared_ptr<hkbFootIkControlsModifier>         buildFootIkControls(const FootIkControlsModifierDef&);
    std::shared_ptr<hkbReferencePoseGenerator>         buildReferencePose(const ReferencePoseGeneratorDef&);
    std::shared_ptr<BSIStateManagerModifier>           buildIStateManager(const BSIStateManagerModifierDef&);
    std::shared_ptr<hkbFootIkModifier>                 buildFootIkModifier(const FootIkModifierDef&);

    // Data-array builder (referenced by hkbEvaluateExpressionModifier).
    std::shared_ptr<hkbExpressionDataArray>            buildExpressionDataArray(const ExpressionDataArrayDef&);

    // Aux builders.
    std::shared_ptr<hkbVariableBindingSet>             buildBindingSet(const std::vector<BindingDef>&);
    std::shared_ptr<hkbBoneWeightArray>                buildBoneWeights(const BoneWeightsDef&);
    std::shared_ptr<hkbBoneIndexArray>                 buildBoneIndexArray(const std::string& name);
    std::shared_ptr<hkbClipTriggerArray>               buildTriggers(const std::vector<ClipTriggerDef>&);
    std::shared_ptr<hkbStateMachineEventPropertyArray> buildEventArray(const std::vector<EventPropertyDef>&);
    std::shared_ptr<hkbStateMachineTransitionInfoArray> buildTransitions(const std::vector<TransitionInfoDef>&);
    std::shared_ptr<hkbTransitionEffect>               buildTransitionEffect(const std::string& name);

    void fillEventBase(hkbEventBase& ev, const InlineEventDef& def);
    void fillEventBase(hkbEventBase& ev, const std::optional<std::string>& name, int id,
                       const std::string& payload);
};

// Stage 4 — bindings-resolve pass (name -> index), run on a MUTABLE BehaviorData
// BEFORE BehaviorBuilder. Pre-resolves every NAMED variable/character-property
// binding to its numeric index and clears the name, producing the index-resolved
// intermediate the builder emits from. Behavior-preserving: any binding it does not
// touch (numeric, or a category not yet covered) still falls through to the builder's
// inline resolve*, so output is byte-identical regardless of coverage. The name->index
// map is context-free, which is what lets the resolution lift out of Build().
void ResolveBehaviorBindings(BehaviorData& data);

} // namespace havok::model
