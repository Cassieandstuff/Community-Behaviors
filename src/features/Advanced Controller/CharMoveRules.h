#pragma once

#include "CharMovementModel.h"

#include <hky/locomotion/Modifiers.h>

#include <string>
#include <vector>

/// @file
/// @brief The movement rules in the game (char_move_rules): the rule files under SKSE/Plugins/Community Behaviors/Movement
///        (HKY-SDK's locomotion/Modifiers.h), evaluated for each character CB's movement model drives, eased, and laid on
///        its frame by the "Rules" controller component.
/// @note  Split in two because the frame needs the rules at two points: the gait cap and the slope's tuning before the
///        frame picks its speed column and slope factor (Step), the speed scales, caps and profile's numbers after the
///        caps exist (the component, on the same state).

namespace CB::feature::controller::rules {

    using hky::locomotion::ModifierState;

    /// @brief Evaluates the rules for `actor` this frame, eases its modifiers by `dt` and keeps the result for the
    ///        component; queues the graph variables the rules set or let go (written on the main thread).
    /// @return the modifiers to apply this frame; neutral while the rules are off or none is loaded.
    /// @note  Once per frame per actor, from its frame driver, before the speed column is picked.
    ModifierState Step(RE::Actor& actor, float dt);

    /// @return the fastest gait the rules allow `actor` now (Sprint when none caps it). Thread-safe.
    Gait GaitCap(RE::FormID actor);

    /// @brief `gait` held to `m`'s gait cap, and `full` (the frame's forward speed for `gait`) moved to the capped gait's.
    /// @note  Run capped to Walk rescales by the type's walk over its run. A sprint capped keeps its speed for the frame or
    ///        two until CB's sprint rules drop the sprint (their "gait cap" reason): the sprint type has no run column.
    Gait CapGait(RE::Actor& actor, const ModifierState& m, Gait gait, float& full);

    /// @brief What the debug HUD shows of the player's rules.
    struct Hud {
        int                      loaded = 0;     ///< rules loaded, live or not
        std::vector<std::string> active;         ///< the ids holding this frame
        std::vector<std::string> errors;         ///< load and rule errors, first few
        ModifierState            state;
    };
    /// @brief The player's rules as of its latest Step; false while the rules are off or did not run within half a second.
    bool Current(Hud& out);

    /// @brief Re-reads the rule files and recompiles them. Thread-safe; the next Step uses the new set.
    void Reload();

}  // namespace CB::feature::controller::rules
