#pragma once

#include <cstdint>

/// @file
/// @brief The player's animation-led jump wind-up: the ground states (CharStateWrapper) hold a jump from the ground until
///        the clip the pose search switched to for it reaches its takeoff (locomotion::Clip::takeoff), at most
///        [Character] fJumpWindup seconds.
/// @note  Why: the impulse waits for the animation's own push-off, the way a fighting game's jump squat does, and the
///        chooser counts the wind-up as airborne so the jump starts play from their first frame. Player only: an NPC's
///        jump keeps the short fixed wind-up.
/// @note  Written by the physics step and the graph update on different threads: every call is atomic.

namespace CB::feature::controller::takeoff {

    /// @brief A ground jump starts winding up; returns its generation, which the node's switch must carry to count.
    std::uint32_t Begin();

    /// @brief The wind-up ended (launched, cancelled, or the character left the ground).
    void End();

    /// @brief Whether a wind-up is pending.
    bool Pending();

    /// @brief The pending wind-up's generation, 0 when none.
    std::uint32_t Generation();

    /// @brief The node's verdict for wind-up `generation`: the clip it switched to since Begin has reached its takeoff
    ///        (or has none, so waiting would show nothing). A stale generation is ignored.
    void Ready(std::uint32_t generation);

    /// @brief Whether the pending wind-up may launch now.
    bool IsReady();

}  // namespace CB::feature::controller::takeoff
