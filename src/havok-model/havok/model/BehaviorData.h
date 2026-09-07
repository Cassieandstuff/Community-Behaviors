#pragma once
#include "havok/model/defs/Defs.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

// BehaviorData — the fully-loaded behavior model, keyed by node name. C++ mirror
// of HKBuild's BehaviorReader.BehaviorData. Produced by YamlBehaviorLoader (from
// the trueflight YAML tree) or constructed directly (the builder_test does this).
// Consumed by BehaviorBuilder to emit the Tier-A object graph.
//
// Pure C++ — no ryml. std::map gives deterministic ordering for reproducibility.

namespace havok::model {

struct BehaviorData {
    BehaviorFile                                            behavior;

    std::map<std::string, ClipGeneratorDef>                 clips;
    std::map<std::string, BlenderGeneratorDef>              blenders;
    std::map<std::string, ManualSelectorDef>                selectors;
    std::map<std::string, StateMachineDef>                  stateMachines;
    std::map<std::string, StateDef>                         states;
    std::map<std::string, TransitionEffectDef>              transitionEffects;
    std::map<std::string, ModifierGeneratorDef>             modifierGenerators;
    std::map<std::string, BSIsActiveModifierDef>            isActiveModifiers;
    std::map<std::string, BSiStateTaggingGeneratorDef>      stateTaggingGenerators;
    std::map<std::string, BehaviorReferenceGeneratorDef>    behaviorReferences;
    std::map<std::string, BGSGamebryoSequenceGeneratorDef>  gamebryoSequences;
    std::map<std::string, ModifierListDef>                  modifierLists;
    std::map<std::string, BSCyclicBlendTransitionGeneratorDef> cyclicBlendGenerators;
    std::map<std::string, EventDrivenModifierDef>           eventDrivenModifiers;
    std::map<std::string, BSEventEveryNEventsModifierDef>   eventEveryNModifiers;
    std::map<std::string, GenericModifierDef>               genericModifiers;
    std::map<std::string, FootIkControlsModifierDef>        footIkControlsModifiers;
    std::map<std::string, EvaluateExpressionModifierDef>    evaluateExpressionModifiers;
    std::map<std::string, BSInterpValueModifierDef>         interpValueModifiers;
    std::map<std::string, EventsFromRangeModifierDef>       eventsFromRangeModifiers;
    std::map<std::string, BSBoneSwitchGeneratorDef>         boneSwitchGenerators;
    std::map<std::string, BSSynchronizedClipGeneratorDef>   synchronizedClips;
    std::map<std::string, BSOffsetAnimationGeneratorDef>    offsetAnimGenerators;
    std::map<std::string, PoseMatchingGeneratorDef>         poseMatchingGenerators;
    std::map<std::string, ReferencePoseGeneratorDef>        referencePoseGenerators;
    std::map<std::string, BSIStateManagerModifierDef>       iStateManagerModifiers;
    std::map<std::string, FootIkModifierDef>                footIkModifiers;
    std::map<std::string, ExpressionDataArrayDef>           expressionDataArrays;
    std::map<std::string, EventRangeDataArrayDef>           eventRangeDataArrays;
    std::map<std::string, BoneIndexArrayDef>                boneIndexArrays;

    std::optional<BehaviorGraphDataDef>                     graphData;

    // Skeleton bone names in index order (empty if no skeleton.yaml found).
    std::vector<std::string>                                boneNames;
    // Behavior-local bone weight presets (bone_presets.yaml).
    std::map<std::string, std::map<std::string, std::string>> bonePresets;

    // Resolve a generator/modifier name to its hk class string (mirrors
    // BehaviorData.GetNodeClass). Returns empty for "null"/unknown.
    std::string GetNodeClass(const std::string& name) const {
        if (name.empty() || name == "null") return {};
        if (clips.count(name))                  return "hkbClipGenerator";
        if (blenders.count(name))               return "hkbBlenderGenerator";
        if (selectors.count(name))              return "hkbManualSelectorGenerator";
        if (stateMachines.count(name))          return "hkbStateMachine";
        if (modifierGenerators.count(name))     return "hkbModifierGenerator";
        if (isActiveModifiers.count(name))      return "BSIsActiveModifier";
        if (stateTaggingGenerators.count(name)) return "BSiStateTaggingGenerator";
        if (behaviorReferences.count(name))     return "hkbBehaviorReferenceGenerator";
        if (gamebryoSequences.count(name))      return "BGSGamebryoSequenceGenerator";
        if (modifierLists.count(name))          return "hkbModifierList";
        if (cyclicBlendGenerators.count(name))  return "BSCyclicBlendTransitionGenerator";
        if (eventDrivenModifiers.count(name))   return "hkbEventDrivenModifier";
        if (eventEveryNModifiers.count(name))   return "BSEventEveryNEventsModifier";
        if (auto it = genericModifiers.find(name); it != genericModifiers.end())
            return it->second.className;
        if (footIkControlsModifiers.count(name))     return "hkbFootIkControlsModifier";
        if (evaluateExpressionModifiers.count(name)) return "hkbEvaluateExpressionModifier";
        if (interpValueModifiers.count(name))        return "BSInterpValueModifier";
        if (eventsFromRangeModifiers.count(name))    return "hkbEventsFromRangeModifier";
        if (boneSwitchGenerators.count(name))   return "BSBoneSwitchGenerator";
        if (synchronizedClips.count(name))      return "BSSynchronizedClipGenerator";
        if (offsetAnimGenerators.count(name))   return "BSOffsetAnimationGenerator";
        if (poseMatchingGenerators.count(name)) return "hkbPoseMatchingGenerator";
        if (referencePoseGenerators.count(name)) return "hkbReferencePoseGenerator";
        if (iStateManagerModifiers.count(name)) return "BSIStateManagerModifier";
        if (footIkModifiers.count(name)) return "hkbFootIkModifier";
        return {};
    }
};

} // namespace havok::model
