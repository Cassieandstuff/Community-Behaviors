#pragma once

#include <PCH.h>

/// @file
/// @brief Game functions the player's camera reads, declared so features can call them and Override<&fn> them.

namespace CB::platform::game {

    /// @brief How far above `actor`'s root the third-person camera pivots when it targets `actor`
    ///        (ThirdPersonState's position math 0x1408e8640 adds it to the target's root).
    /// @note For the player, the Camera3rd [Cam3] node's local z (animations keying it move the camera); for any
    ///       other actor, a height its process caches, else one from its race. A horse has no camera node: riding,
    ///       vanilla's camera targets the horse and pivots at the horse's root plus this.
    float Actor_GetCameraHeight(RE::Actor* actor);

}  // namespace CB::platform::game
