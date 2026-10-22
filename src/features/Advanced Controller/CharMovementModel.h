#pragma once

#include "CharFacing.h"

#include <cstdint>

/// @file
/// @brief The player's movement model, matched to Unreal's Game Animation Sample (its CMC character): one pure step
///        shared by the frame (CharFacing: real dt, live input) and the predictor (its substeps, input frozen), so the
///        movement predicted is the movement played.
/// @note  World space, game units, Skyrim headings: radians clockwise from +y, so heading h points along (sin h, cos h).
///        GASP's centimetre values are converted at 0.7 game units per cm.
/// @note  The one deliberate departure from GASP: the facing is the controller's, never the animation's. GASP snaps the
///        capsule and smooths only the visual root (Offset Root Bone); in Skyrim the actor's heading is the gameplay
///        heading (TDM's yaw, combat, AI), so the smoothing is a facing spring here.

namespace CB::feature::controller::move {

    /// @brief TDM's directional mode orients to movement; its target lock strafes.
    enum class Mode : std::uint8_t { Orient, Strafe };

    /// @brief CMC's walking numbers as GASP's UpdateMovement_PreCMC sets them every tick, converted to game units.
    constexpr float kAccel             = 560.0f;    ///< MaxAcceleration (800 cm/s^2), walk and run
    constexpr float kSprintAccelSlow   = 210.0f;    ///< sprint: MaxAcceleration eases 560 -> 210 ...
    constexpr float kSprintAccelFrom   = 210.0f;    ///< ... as the speed goes from here (300 cm/s) ...
    constexpr float kSprintAccelTo     = 490.0f;    ///< ... to here (700 cm/s)
    constexpr float kFriction          = 5.0f;      ///< GroundFriction (1/s), walk and run
    constexpr float kSprintFrictionLow = 3.0f;      ///< sprint: GroundFriction eases 5 -> 3 ...
    constexpr float kSprintFrictionTo  = 350.0f;    ///< ... as the speed goes from 0 to here (500 cm/s)
    constexpr float kBrakingNoInput    = 1400.0f;   ///< BrakingDecelerationWalking without input (2000 cm/s^2)
    constexpr float kBrakingWithInput  = 350.0f;    ///< ... with input, i.e. above the cap after a gait drop (500 cm/s^2)
    /// @brief BrakingFrictionFactor: braking friction = ground friction x this (Unreal's default; GASP's class defaults
    ///        were not exported, so the default is assumed).
    constexpr float kBrakingFrictionFactor = 2.0f;
    constexpr float kBrakingSubstep        = 1.0f / 33.0f;   ///< CMC's BrakingSubStepTime
    constexpr float kBrakeToStop           = 7.0f;           ///< BRAKE_TO_STOP_VELOCITY (10 cm/s)
    constexpr float kOverMaxTolerance      = 1.01f;          ///< IsExceedingMaxSpeed's 1% slack

    constexpr float kFacingHalflife = 0.1f;          ///< GASP's Offset Root Bone rotation halflife, now the facing's
    constexpr float kAirTurnRate    = 3.4906585f;    ///< 200 deg/s: GASP's falling RotationRate, the facing's cap in the air
    constexpr float kFacingMinSpeed = 10.0f;         ///< below this speed the facing holds (no direction to chase)
    constexpr float kSprintCone     = 0.87266463f;   ///< 50 deg: strafing sprints only within this of the facing (CanSprint)

    /// @brief The speed caps the step works under.
    struct Caps {
        /// @brief The engine's forward speed for the current gait, every multiplier kept (the frame hands the engine
        ///        forward-only input), times the slope factor.
        float forward = 0.0f;
        float left = 1.0f, right = 1.0f, back = 1.0f;   ///< MOVT's direction speeds as fractions of forward (strafe only)
    };

    struct Input {
        Mode  mode      = Mode::Orient;
        Gait  gait      = Gait::None;
        bool  airborne  = false;   ///< the ground states' Airborne: the velocity is the air control's, only the facing steps
        bool  haveStick = false;
        float stickX = 0.0f, stickY = 0.0f;   ///< the direction asked for: world, unit length (fixed-speed stick)
        bool  haveTarget = false;
        float targetX = 0.0f, targetY = 0.0f;   ///< strafe: the lock target's world position
    };

    struct State {
        float vx = 0.0f, vy = 0.0f;   ///< velocity, game units/s
        float yaw     = 0.0f;         ///< the facing, a heading kept unwrapped near the actor's
        float yawRate = 0.0f;         ///< radians/s, clockwise positive
    };

    /// @brief GASP's Curve_StrafeSpeedMap: the angle between velocity and facing (0..pi) to 0 forward, 1 side, 2 back
    ///        (keys 0/45 -> 0, 80/100 -> 1, 135/180 -> 2, linear between).
    float StrafeSpeedMap(float angle);

    /// @brief The max speed moving along heading `velYaw` while facing `yaw`: the forward cap when orienting to movement;
    ///        when strafing, forward blended to MOVT's side (left or right by the angle's sign) and back speeds by
    ///        StrafeSpeedMap.
    float MaxSpeed(const Caps& caps, Mode mode, float velYaw, float yaw);

    /// @brief One step of `dt` seconds for a character at (px, py).
    /// @note  On the ground, CMC's CalcVelocity: without input, or above the cap, braking (substepped, friction x
    ///        kBrakingFrictionFactor, kBrakingNoInput or kBrakingWithInput), never braking below the cap while still
    ///        pushing forward; otherwise friction swings the velocity toward the stick at its current speed; then the
    ///        stick's acceleration, clamped to the cap. Airborne leaves the velocity alone. Then the facing: a spring
    ///        (kFacingHalflife) toward the velocity's heading (strafe: toward the target from (px, py)), its rate capped at
    ///        kAirTurnRate while airborne; it holds below kFacingMinSpeed.
    void Step(State& s, const Input& in, const Caps& caps, float px, float py, float dt);

    /// @brief Whether the direction allows sprinting: always when orienting to movement; when strafing, only while the
    ///        stick is within kSprintCone of the facing (GASP's CanSprint).
    bool SprintDirectionAllowed(const State& s, const Input& in);

    /// @brief The latest frame's model: the state after its step, and what it stepped with.
    struct Snapshot {
        State state;
        Input input;
        Caps  caps;
        float px = 0.0f, py = 0.0f;   ///< the player's position that frame
    };
    /// @brief False while the model is off (bMovementModel, or no TDM) or no frame ran within the last half second.
    /// @note  Thread-safe: the frame writes it; the predictor (physics), the sprint rules and the HUD read it.
    bool Current(Snapshot& out);
    /// @brief The frame's half of Current.
    void Publish(const Snapshot& snap);

    /// @brief Whether the movement model runs (char_movement_model).
    bool On();

}  // namespace CB::feature::controller::move
