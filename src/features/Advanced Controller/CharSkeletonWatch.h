#pragma once

#include <optional>

/// @file
/// @brief The vanishing-actor diagnostics' skeleton watch: four bones of the player every frame, and of each high-process
///        actor every 0.25 s, must be finite and near their actor, and the actor's world bound finite. A failure is
///        logged, and sent to Tracy, once per episode per actor; recovery ends the episode.
/// @note  Why: actors vanished at random (invisible, not A-posed) once the dead blend shipped; a bone or bound gone
///        non-finite or flung away is the suspected path to that, not a proven one.

namespace CB::feature::controller::motion {

    /// @brief Queues one watch pass on the main thread (an SKSE task), where the scene graph is safe to walk; a pass
    ///        already queued is not queued twice.
    /// @note  Called from motion matching's frame hook for the player, so the watch runs while motion matching is on.
    void QueueSkeletonWatch();

    /// @brief Whether `character` (an hkbContext::m_character) is the hkbCharacter of one of the player's behavior
    ///        graphs, as the last watch pass saw them; nullopt before the first pass.
    std::optional<bool> IsPlayerCharacter(const void* character);

}  // namespace CB::feature::controller::motion
