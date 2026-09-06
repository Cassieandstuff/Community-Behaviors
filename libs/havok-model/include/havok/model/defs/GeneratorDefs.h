#pragma once
#include "havok/model/defs/CommonDefs.h"

#include <optional>
#include <string>
#include <vector>

// Tier-B Def POCOs — generator node kinds.
// Faithful ports of the generator HKBuild\src\Models\*Def.cs files.

namespace havok::model {

// ── ClipGeneratorDef.cs ───────────────────────────────────────────────────────
struct ClipTriggerDef {
    std::string                localTime = "0.000000";
    int                        eventId   = -1;
    std::optional<std::string> event;
    std::string                payload   = "null";
    bool                       relativeToEndOfClip = false;
    bool                       acyclic             = false;
    bool                       isAnnotation        = false;
};

struct ClipGeneratorDef {
    std::string                 className = "hkbClipGenerator";
    std::string                 name;
    std::string                 animationName;
    std::string                 mode                       = "MODE_SINGLE_PLAY";
    std::string                 playbackSpeed              = "1.000000";
    std::string                 cropStartAmountLocalTime   = "0.000000";
    std::string                 cropEndAmountLocalTime     = "0.000000";
    std::string                 startTime                  = "0.000000";
    std::string                 enforcedDuration           = "0.000000";
    std::string                 userControlledTimeFraction = "0.000000";
    int                         animationBindingIndex      = -1;
    int                         flags                      = 0;
    int                         userData                   = 0;
    std::optional<std::vector<ClipTriggerDef>> triggers;
    std::optional<std::vector<BindingDef>>     bindings;   // e.g. playbackSpeed -> weaponSpeedMult
};

// ── BlenderGeneratorDef.cs ────────────────────────────────────────────────────
struct BlenderChildDef {
    std::string                   generator;
    std::string                   weight               = "0.000000";
    std::string                   worldFromModelWeight = "1.000000";
    std::optional<BoneWeightsDef> boneWeights;
    std::optional<std::vector<BindingDef>> bindings;  // hkbBlenderGeneratorChild is bindable
};

struct BlenderGeneratorDef {
    std::string                 className = "hkbBlenderGenerator";
    std::string                 name;
    int                         flags             = 0;
    bool                        subtractLastChild = false;
    int                         userData          = 0;
    std::string                 referencePoseWeightThreshold = "0.000000";
    std::string                 blendParameter               = "1.000000";
    std::string                 minCyclicBlendParameter      = "0.000000";
    std::string                 maxCyclicBlendParameter      = "1.000000";
    int                         indexOfSyncMasterChild       = -1;
    std::optional<std::vector<BindingDef>> bindings;
    std::vector<BlenderChildDef>           children;
};

// ── ManualSelectorDef.cs ──────────────────────────────────────────────────────
struct ManualSelectorDef {
    std::string                 className = "hkbManualSelectorGenerator";
    std::string                 name;
    int                         selectedGeneratorIndex = 0;
    int                         currentGeneratorIndex  = 0;
    int                         userData               = 0;
    std::optional<std::vector<BindingDef>> bindings;
    std::vector<std::string>               generators;
};

// ── BSCyclicBlendTransitionGeneratorDef.cs ────────────────────────────────────
struct BSCyclicBlendTransitionGeneratorDef {
    std::string                 className = "BSCyclicBlendTransitionGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 pBlenderGenerator;
    InlineEventDef              eventToFreezeBlendValue;
    InlineEventDef              eventToCrossBlend;
    std::string                 fBlendParameter     = "0.000000";
    std::string                 fTransitionDuration = "0.200000";
    std::string                 eBlendCurve         = "BLEND_CURVE_SMOOTH";
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BSiStateTaggingGeneratorDef.cs ────────────────────────────────────────────
struct BSiStateTaggingGeneratorDef {
    std::string                 className = "BSiStateTaggingGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 pDefaultGenerator;
    int                         iStateToSetAs = 0;
    int                         iPriority     = 0;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BehaviorReferenceGeneratorDef.cs ──────────────────────────────────────────
struct BehaviorReferenceGeneratorDef {
    std::string                 className = "hkbBehaviorReferenceGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 behaviorName;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BGSGamebryoSequenceGenerator ──────────────────────────────────────────────
struct BGSGamebryoSequenceGeneratorDef {
    std::string                 className = "BGSGamebryoSequenceGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 sequence;                          // m_pSequence (.kf name)
    std::string                 blendModeFunction = "BMF_PERCENT"; // enum BlendModeFunction
    std::string                 percent           = "1.000000";    // m_fPercent
    std::optional<std::vector<BindingDef>> bindings;
};

// ── hkbReferencePoseGenerator ─────────────────────────────────────────────────
// Outputs the skeleton's reference pose. No authored fields beyond the generator
// base (m_skeleton is SERIALIZE_IGNORED / always null in behavior packfiles).
struct ReferencePoseGeneratorDef {
    std::string                 className = "hkbReferencePoseGenerator";
    std::string                 name;
    int                         userData = 0;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BSBoneSwitchGeneratorDef.cs ───────────────────────────────────────────────
struct BoneSwitchChildDef {
    std::string                   pGenerator;
    std::optional<BoneWeightsDef> boneWeights;
    std::optional<std::vector<BindingDef>> bindings;
};

struct BSBoneSwitchGeneratorDef {
    std::string                 className = "BSBoneSwitchGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 pDefaultGenerator;
    std::optional<std::vector<BindingDef>>       bindings;
    std::optional<std::vector<BoneSwitchChildDef>> children;
};

// ── BSSynchronizedClipGeneratorDef.cs ─────────────────────────────────────────
struct BSSynchronizedClipGeneratorDef {
    std::string                 className = "BSSynchronizedClipGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 pClipGenerator;
    std::string                 syncAnimPrefix;
    bool                        bSyncClipIgnoreMarkPlacement = false;
    std::string                 fGetToMarkTime      = "0.000000";
    std::string                 fMarkErrorThreshold = "0.100000";
    bool                        bLeadCharacter        = false;
    bool                        bReorientSupportChar  = false;
    bool                        bApplyMotionFromRoot  = false;
    int                         sAnimationBindingIndex = -1;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BSOffsetAnimationGeneratorDef.cs ──────────────────────────────────────────
struct BSOffsetAnimationGeneratorDef {
    std::string                 className = "BSOffsetAnimationGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 pDefaultGenerator;
    std::string                 pOffsetClipGenerator;
    std::string                 fOffsetVariable   = "0.000000";
    std::string                 fOffsetRangeStart = "0.000000";
    std::string                 fOffsetRangeEnd   = "1.000000";
    std::optional<std::vector<BindingDef>> bindings;
};

// ── PoseMatchingGeneratorDef.cs ───────────────────────────────────────────────
struct PoseMatchingGeneratorDef {
    std::string                 className = "hkbPoseMatchingGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 referencePoseWeightThreshold = "0.000000";
    std::string                 blendParameter               = "0.000000";
    std::string                 minCyclicBlendParameter      = "0.000000";
    std::string                 maxCyclicBlendParameter      = "1.000000";
    int                         indexOfSyncMasterChild       = -1;
    std::string                 flags             = "0";
    bool                        subtractLastChild = false;
    std::optional<std::vector<BlenderChildDef>> children;
    std::string                 worldFromModelRotation = "(0.000000 0.000000 0.000000 1.000000)";
    std::string                 blendSpeed             = "1.000000";
    std::string                 minSpeedToSwitch       = "0.200000";
    std::string                 minSwitchTimeNoError   = "0.200000";
    std::string                 minSwitchTimeFullError = "0.000000";
    int                         startPlayingEventId = -1;
    std::optional<std::string>  startPlayingEvent;
    int                         startMatchingEventId = -1;
    std::optional<std::string>  startMatchingEvent;
    int                         rootBoneIndex    = 0;
    int                         otherBoneIndex   = 0;
    int                         anotherBoneIndex = 0;
    int                         pelvisIndex      = 0;
    std::string                 mode = "MODE_MATCH";
    std::optional<std::vector<BindingDef>> bindings;
};

} // namespace havok::model
