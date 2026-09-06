#pragma once
#include "havok/model/defs/CommonDefs.h"

#include <optional>
#include <string>
#include <vector>

// Tier-B Def POCOs — state machine, state info, transition effect, transitions.
// Faithful ports of HKBuild\src\Models\StateMachineDef.cs and TransitionDef.cs.

namespace havok::model {

// ── TransitionDef.cs — TransitionIntervalDef ──────────────────────────────────
struct TransitionIntervalDef {
    int                        enterEventId = -1;
    std::optional<std::string> enterEvent;
    int                        exitEventId  = -1;
    std::optional<std::string> exitEvent;
    std::string                enterTime = "0.000000";
    std::string                exitTime  = "0.000000";
};

// ── TransitionDef.cs — TransitionInfoDef ──────────────────────────────────────
// One hkbStateMachineTransitionInfo. `toState` is resolved to `toStateId` at load
// time by the reader; `event` resolved to `eventId` at build time.
struct TransitionInfoDef {
    TransitionIntervalDef       triggerInterval;
    TransitionIntervalDef       initiateInterval;
    std::string                 transition;          // transition-effect name
    std::optional<std::string>  condition;           // hkbExpressionCondition name
    std::optional<std::string>  conditionString;     // hkbStringCondition value
    int                         eventId = -1;
    std::optional<std::string>  event;
    int                         toStateId = 0;
    std::optional<std::string>  toState;
    int                         fromNestedStateId = 0;
    int                         toNestedStateId   = 0;
    int                         priority          = 0;
    std::string                 flags = "FLAG_DISABLE_CONDITION";
};

// ── StateMachineDef.cs — StateMachineDef ──────────────────────────────────────
struct StateMachineDef {
    std::string                 className = "hkbStateMachine";
    std::string                 name;
    int                         userData     = 0;
    int                         startStateId = 0;
    // Event the SM fires whenever its state/transition changes — combat frameworks
    // (BFCO) hang motion-lock / state-notify logic off it. Real serialized hkbEvent
    // id; vanilla leaves it -1 (which is why vanilla round-tripped fine). Dropping it
    // defaults to -1 and breaks BFCO's wind-down motion lock (in-game float/slide).
    int                         eventToSendWhenStateOrTransitionChangesId = -1;
    std::optional<std::string>  eventToSendWhenStateOrTransitionChangesEvent;
    int                         returnToPreviousStateEventId       = -1;
    std::optional<std::string>  returnToPreviousStateEvent;
    int                         randomTransitionEventId            = -1;
    std::optional<std::string>  randomTransitionEvent;
    int                         transitionToNextHigherStateEventId = -1;
    std::optional<std::string>  transitionToNextHigherStateEvent;
    int                         transitionToNextLowerStateEventId  = -1;
    std::optional<std::string>  transitionToNextLowerStateEvent;
    int                         syncVariableIndex = -1;
    std::optional<std::string>  syncVariable;
    bool                        wrapAroundStateId          = false;
    int                         maxSimultaneousTransitions = 32;
    std::string                 startStateMode     = "START_STATE_MODE_DEFAULT";
    std::string                 selfTransitionMode = "SELF_TRANSITION_MODE_NO_TRANSITION";
    // Parsed wildcard transitions from the SM's inline `transitions:` block.
    std::optional<std::vector<TransitionInfoDef>> parsedWildcardTransitions;
    std::optional<std::vector<BindingDef>>        bindings;
    std::vector<std::string>                      states;  // state names in order
};

// ── StateMachineDef.cs — EventPropertyDef ─────────────────────────────────────
// One hkbEventProperty used in enter/exit notify event arrays.
struct EventPropertyDef {
    int                        id = -1;
    std::optional<std::string> event;
    std::string                payload = "null";
};

// ── StateMachineDef.cs — StateDef ─────────────────────────────────────────────
struct StateDef {
    std::string                 className = "hkbStateMachineStateInfo";
    std::string                 name;
    int                         stateId = 0;
    std::string                 generator;
    std::string                 probability = "1.000000";
    bool                        enable      = true;
    std::optional<std::vector<EventPropertyDef>>  enterNotifyEvents;
    std::optional<std::vector<EventPropertyDef>>  exitNotifyEvents;
    std::optional<std::vector<TransitionInfoDef>> parsedTransitions;
    // Transition inversion (native merge): wildcard transitions that lead INTO this state
    // from its parent SM. When the merge inserts this state into a `parents:` SM, it also
    // appends these to that SM's wildcard transitions — so a child adds the trigger that
    // reaches its state WITHOUT editing (or name↔id-matching) the shared, id-keyed vanilla
    // SM. The mirror of `parents` for triggers: `parents` inserts the state, `entryTransitions`
    // inserts the wildcards that enter it. Their `toState` defaults to this state's own name.
    std::optional<std::vector<TransitionInfoDef>> entryTransitions;
    std::optional<std::vector<BindingDef>>        bindings;
    // Ownership inversion (native merge): a state may declare the state machine(s) it
    // belongs to by id. The loader COLLECTS states into each SM's `states` list after
    // merge, so a mod adds a state as its own namespaced file (naming its parent SM)
    // without editing — or colliding on — the shared SM node. Empty for decompiled
    // states (their SM lists them explicitly); populated by mod deltas / the converter.
    std::vector<std::string>                      parents;
};

// ── TransitionDef.cs — TransitionEffectDef ────────────────────────────────────
struct TransitionEffectDef {
    std::string                 className = "hkbBlendingTransitionEffect";
    std::string                 name;
    int                         userData = 0;
    std::string                 selfTransitionMode = "SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC";
    std::string                 eventMode          = "EVENT_MODE_DEFAULT";
    std::string                 duration           = "0.200000";
    std::string                 toGeneratorStartTimeFraction = "0.000000";
    std::string                 flags      = "0";
    std::string                 endMode    = "END_MODE_NONE";
    std::string                 blendCurve = "BLEND_CURVE_SMOOTH";
    // Read/serialized on hkbBlendingTransitionEffect but historically NOT emitted.
    // initializeCharacterPose resets the character's world-from-model (root transform)
    // at transition start; BFCO relies on its value to carry attack root motion through
    // the wind-down blend, so defaulting it floats the recovery. Both default false
    // (= vanilla, which is why vanilla still round-trips byte-identical).
    bool                        applySelfTransition     = false;
    bool                        initializeCharacterPose = false;
    std::optional<std::vector<BindingDef>> bindings;
};

} // namespace havok::model
