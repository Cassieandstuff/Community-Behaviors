#pragma once
#include "havok/model/defs/CommonDefs.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// Tier-B Def POCOs — modifier node kinds + data arrays.
// Faithful ports of the modifier HKBuild\src\Models\*Def.cs files.

namespace havok::model {

// ── ModifierGeneratorDef.cs ───────────────────────────────────────────────────
struct ModifierGeneratorDef {
    std::string                 className = "hkbModifierGenerator";
    std::string                 name;
    int                         userData = 0;
    std::string                 modifier;
    std::string                 generator;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── ModifierListDef.cs ────────────────────────────────────────────────────────
struct ModifierListDef {
    std::string                 className = "hkbModifierList";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    std::vector<std::string>    modifiers;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BSIsActiveModifierDef.cs ──────────────────────────────────────────────────
struct BSIsActiveModifierDef {
    std::string                 className = "BSIsActiveModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    bool bIsActive0 = false, bInvertActive0 = false;
    bool bIsActive1 = false, bInvertActive1 = false;
    bool bIsActive2 = false, bInvertActive2 = false;
    bool bIsActive3 = false, bInvertActive3 = false;
    bool bIsActive4 = false, bInvertActive4 = false;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── EventDrivenModifierDef.cs ─────────────────────────────────────────────────
struct EventDrivenModifierDef {
    std::string                 className = "hkbEventDrivenModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    std::string                 modifier;
    int                         activateEventId   = -1;
    std::optional<std::string>  activateEvent;
    int                         deactivateEventId = -1;
    std::optional<std::string>  deactivateEvent;
    bool                        activeByDefault = false;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BSEventEveryNEventsModifierDef.cs ─────────────────────────────────────────
struct BSEventEveryNEventsModifierDef {
    std::string                 className = "BSEventEveryNEventsModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    InlineEventDef              eventToCheckFor;
    InlineEventDef              eventToSend;
    int                         numberOfEventsBeforeSend        = 1;
    int                         minimumNumberOfEventsBeforeSend = 1;
    bool                        randomizeNumberOfEvents = false;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BSInterpValueModifierDef.cs ───────────────────────────────────────────────
// Damps/interpolates a value: result = interp(source, target, gain). Vanilla's
// SpeedDamped modifier binds source/target/result to Speed/SpeedDamped variables.
struct BSInterpValueModifierDef {
    std::string                 className = "BSInterpValueModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    std::string                 source   = "0.000000";
    std::string                 target   = "0.000000";
    std::string                 result   = "0.000000";
    std::string                 gain     = "0.000000";
    std::optional<std::vector<BindingDef>> bindings;
};

// ── EvaluateExpressionModifierDef.cs ──────────────────────────────────────────
struct EvaluateExpressionModifierDef {
    std::string                 className = "hkbEvaluateExpressionModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    std::string                 expressions;   // referenced hkbExpressionDataArray name
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BSIStateManagerModifierDef ────────────────────────────────────────────────
// Drives an iState variable from a set of (state machine, state id) tuples.
struct IStateDataDef {
    std::string pStateMachine;   // referenced hkbStateMachine node name
    int         StateID       = 0;
    int         iStateToSetAs = 0;
};

struct BSIStateManagerModifierDef {
    std::string                 className = "BSIStateManagerModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    int                         iStateVar = 0;         // raw index (fallback)
    std::optional<std::string>  iStateVariable;        // named (preferred); resolved to index
    std::vector<IStateDataDef>  stateData;
    std::optional<std::vector<BindingDef>> bindings;
};

// ── GenericModifierDef.cs ─────────────────────────────────────────────────────
// Generic modifiers carry a `class` plus arbitrary extra params. BehaviorBuilder
// dispatches on `className` and reads the params by name.
enum class GenericParamKind { Scalar, Reference, InlineEvent, RefList, InlineObjectList };

struct GenericInlineObjectEntry {
    std::vector<std::pair<std::string, std::string>> fields;
};

struct GenericParam {
    std::string                              name;
    GenericParamKind                         kind = GenericParamKind::Scalar;
    std::optional<std::string>               scalarValue;
    std::optional<std::string>               refValue;
    std::optional<InlineEventDef>            eventValue;
    std::optional<std::vector<std::string>>  refListValue;
    std::optional<std::vector<GenericInlineObjectEntry>> inlineObjectListValue;
};

struct GenericModifierDef {
    std::string                 className;   // the actual hk class, e.g. "hkbTwistModifier"
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    std::optional<std::vector<BindingDef>> bindings;
    std::vector<GenericParam>   extraParams;
};

// ── FootIkControlsModifierDef.cs ──────────────────────────────────────────────
struct FootIkGainsDef {
    float onOffGain = 0.f, groundAscendingGain = 0.f, groundDescendingGain = 0.f;
    float footPlantedGain = 0.f, footRaisedGain = 0.f, footUnlockGain = 0.f;
    float worldFromModelFeedbackGain = 0.f, errorUpDownBias = 0.f, alignWorldFromModelGain = 0.f;
    float hipOrientationGain = 0.f, maxKneeAngleDifference = 0.f, ankleOrientationGain = 0.f;
};

struct FootIkControlDataDef {
    FootIkGainsDef gains;
};

struct FootIkControlsModifierLegDef {
    std::string                 groundPosition = "(0.000000 0.000000 0.000000 0.000000)";
    std::optional<InlineEventDef> ungroundedEvent;
    float                       verticalError = 0.f;
    bool                        hitSomething  = false;
    bool                        isPlantedMS   = false;
};

struct FootIkControlsModifierDef {
    std::string                 className = "hkbFootIkControlsModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    std::optional<std::vector<BindingDef>> bindings;
    FootIkControlDataDef        controlData;
    std::optional<std::vector<FootIkControlsModifierLegDef>> legs;
    std::string                 errorOutTranslation     = "(0.000000 0.000000 0.000000 0.000000)";
    std::string                 alignWithGroundRotation = "(0.000000 0.000000 0.000000 0.000000)";
};

// ── hkbFootIkModifier (full foot IK: gains + per-leg ankle placement) ─────────
struct FootIkModifierLegDef {
    std::string  prevAnkleRotLS = "(0.000000 0.000000 0.000000 1.000000)";
    std::string  kneeAxisLS     = "(0.000000 0.000000 0.000000 0.000000)";
    std::string  footEndLS      = "(0.000000 0.000000 0.000000 0.000000)";
    std::optional<InlineEventDef> ungroundedEvent;
    float footPlantedAnkleHeightMS = 0.f;
    float footRaisedAnkleHeightMS  = 0.f;
    float maxAnkleHeightMS = 0.f;
    float minAnkleHeightMS = 0.f;
    float maxKneeAngleDegrees = 0.f;
    float minKneeAngleDegrees = 0.f;
    float verticalError = 0.f;
    float maxAnkleAngleDegrees = 0.f;
    int   hipIndex   = 0;
    int   kneeIndex  = 0;
    int   ankleIndex = 0;
    bool  hitSomething = false;
    bool  isPlantedMS  = false;
    bool  isOriginalAnkleTransformMSSet = false;
};

struct FootIkModifierDef {
    std::string                 className = "hkbFootIkModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    std::optional<std::vector<BindingDef>> bindings;
    FootIkGainsDef              gains;   // reused from FootIkControlsModifierDef
    std::vector<FootIkModifierLegDef> legs;
    float         raycastDistanceUp      = 0.f;
    float         raycastDistanceDown    = 0.f;
    float         originalGroundHeightMS = 0.f;
    float         errorOut               = 0.f;
    std::string   errorOutTranslation     = "(0.000000 0.000000 0.000000 0.000000)";
    std::string   alignWithGroundRotation = "(0.000000 0.000000 0.000000 1.000000)";
    float         verticalOffset       = 0.f;
    unsigned      collisionFilterInfo  = 0;
    float         forwardAlignFraction = 0.f;
    float         sidewaysAlignFraction = 0.f;
    float         sidewaysSampleWidth  = 0.f;
    bool          useTrackData         = false;
    bool          lockFeetWhenPlanted  = false;
    bool          useCharacterUpVector = false;
    int           alignMode            = 0;
};

// ── ExpressionDataArrayDef.cs ─────────────────────────────────────────────────
struct ExpressionDataDef {
    std::string                 expression;
    int                         assignmentVariableIndex = -1;
    std::optional<std::string>  assignmentVariable;
    int                         assignmentEventIndex = -1;
    std::optional<std::string>  assignmentEvent;
    std::string                 eventMode = "EVENT_MODE_SEND_ONCE";
};

struct ExpressionDataArrayDef {
    std::string                 className = "hkbExpressionDataArray";
    std::string                 name;
    std::vector<ExpressionDataDef> expressionsData;
};

// ── EventRangeDataArrayDef.cs ─────────────────────────────────────────────────
struct EventRangeDef {
    std::string                 upperBound = "0.000000";
    int                         eventId    = -1;
    std::optional<std::string>  event;
    std::optional<std::string>  payload;
    std::string                 eventMode = "EVENT_MODE_SEND_ONCE";
};

struct EventRangeDataArrayDef {
    std::string                 className = "hkbEventRangeDataArray";
    std::string                 name;
    std::vector<EventRangeDef>  eventData;
};

// hkbEventsFromRangeModifier: sends events based on where inputValue lands in the
// referenced hkbEventRangeDataArray's ranges. The array is a data/ sidecar keyed
// by the modifier's name (vanilla convention, as with hkbEvaluateExpressionModifier).
struct EventsFromRangeModifierDef {
    std::string                 className = "hkbEventsFromRangeModifier";
    std::string                 name;
    int                         userData = 0;
    bool                        enable   = true;
    std::string                 inputValue = "0.000000";
    std::string                 lowerBound = "0.000000";
    std::optional<std::string>  eventRanges;   // referenced array name, or null
    std::optional<std::vector<BindingDef>> bindings;
};

// ── BoneIndexArrayDef.cs ──────────────────────────────────────────────────────
struct BoneIndexArrayDef {
    std::string                 className = "hkbBoneIndexArray";
    std::string                 name;
    std::vector<std::string>    boneNames;    // source bone names, resolved in the builder
    std::vector<int>            boneIndices;  // resolved raw indices
};

} // namespace havok::model
