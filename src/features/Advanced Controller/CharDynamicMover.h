#pragma once

#include "CharGround.h"

#include <hky/locomotion/DynamicMover.h>

#include <cstdint>

/// @file
/// @brief The player's Box3D dynamic mover in Skyrim's Havok world (char_dynamic_mover, `[Character] bDynamicMover`): a
///        dynamic capsule of CB's own on the player's collision layer and system group, floating on a pogo joint and
///        driven by a mover joint (hky::locomotion::MoverSubstep), run after every Havok step from a world
///        post-simulation listener, as Havok's own character rigid body sets its velocities. The probes run in the
///        controller's update (where vanilla queries); the joints only touch the body's velocity.
/// @note  Bethesda's proxy rides along as a passenger: each step it is put at the body's feet and asked for no motion
///        of its own, so the actor, saves, the camera and every vanilla reader see the feet, and floor triggers (which
///        count only an actor's layer-30 volume, FUN_1403ffa10) still meet its phantom there.
/// @note  Experimental, AE only (the body and listener pins are AE's). The ground states' mode machine and contract stay
///        CB's (CharStateWrapper's DynamicStep); this file owns the body.

namespace CB::feature::controller::dynamic {

    /// @brief Whether the dynamic mover is on (char_dynamic_mover), read once.
    bool On();

    /// @brief What the controller asks of the mover this step (Havok units).
    struct In {
        float dx = 0, dy = 0;     ///< the requested horizontal velocity
        bool  jump = false;       ///< a jump is asked for (taken only from walkable ground)
        bool  airJump = false;    ///< a fresh press of Jump in the air (taken only while air jumps are left)
        bool  jumpHeld = true;    ///< Jump is down; true while variable jumps are off, so every jump is full
        bool  variable = false;   ///< short hops are on (char_variable_jump)
        float jumpHeight = 0;     ///< the controller's own full height (m_jumpHeight), used while fJumpHeight is unset
        float gravity = 9.80665f; ///< the world's gravity's magnitude times the controller's multiplier
        float walkableZ = 0.682f;
    };

    /// @brief What the mover's latest Havok step left (Havok units).
    struct Out {
        bool  onGround = false, walkable = false, jumping = false;
        bool  jumped = false;      ///< a jump from the ground launched since the last controller step
        bool  airJumped = false;   ///< an air jump launched since the last controller step
        int   airJumpsLeft = 0;
        bool  hopPending = false;  ///< the current jump may still stop at the short hop
        float fullHeight = 0;      ///< the full height of a jump from the ground now (Havok units)
        float nx = 0, ny = 0, nz = 1;   ///< the floor's normal
        float feetX = 0, feetY = 0, feetZ = 0;   ///< where the feet are with the pogo at rest
        float vx = 0, vy = 0, vz = 0;
        float pogoLength = 0;
        hky::locomotion::MoverProbes probes;
        float capsuleA[3]{}, capsuleB[3]{}, radius = 0;   ///< the body's capsule in world space, for drawing
    };

    /// @brief Runs the player's mover for this controller step: creates its body when there is none in this world, puts
    ///        the proxy at the body's feet, probes the floor and hands the request to the next Havok steps.
    /// @return false when it cannot run (no world, the body could not be made): the caller keeps today's path.
    /// @pre   `ctrl` is the player's proxy controller, inside its update (UpdatePhysics), outside the physics step.
    bool Step(::bhkCharacterController& ctrl, const Hull& hull, const In& in, Out& out);

    /// @brief The body's capsule for the debug HUD; false when there is no body.
    bool BodyCapsule(const ::bhkCharacterController& ctrl, float a[3], float b[3], float& radius);

}  // namespace CB::feature::controller::dynamic
