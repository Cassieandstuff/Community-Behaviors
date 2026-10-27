#pragma once

#include <cmath>
#include <cstdint>

/// @file
/// @brief CB::engine::locomotion — a character's movement model, matched to Unreal's Game Animation Sample (its CMC
///        character): one pure step, shared by every caller that moves a character (the game's frame drivers with real
///        dt and live input, a predictor's substeps with input frozen, a game-free editor), so the movement predicted
///        is the movement played.
/// @note  World space, game units, Skyrim headings: radians clockwise from +y, so heading h points along (sin h, cos h).
///        GASP's centimetre values are converted at 0.7 game units per cm.
/// @note  The one deliberate departure from GASP: the facing is the controller's, never the animation's. GASP snaps the
///        capsule and smooths only the visual root (Offset Root Bone); in Skyrim the actor's heading is the gameplay
///        heading (CB's facing, combat, AI), so the smoothing is a facing spring here.

namespace CB::engine::locomotion {

    /// @brief The actor's gait (ActorState: sprinting before running before walking); None while none is set.
    enum class Gait : std::uint8_t { None, Walk, Run, Sprint };

    /// @brief `a` wrapped into (-pi, pi].
    inline float WrapPi(float a)
    {
        constexpr float kPi = 3.14159265f, kTwoPi = 6.2831853f;
        a = std::fmod(a + kPi, kTwoPi);
        if (a < 0.0f) a += kTwoPi;
        return a - kPi;
    }

    /// @brief Exact critically damped spring of `x` (rate `v`) toward `goal`, frame-rate independent by construction.
    /// @param halflife seconds for the gap to halve.
    /// @note The spring-damper in its exact form (as in Holden's motion-matching write-ups): y = 2 ln2 / halflife.
    inline void SpringDamperExact(float& x, float& v, float goal, float halflife, float dt)
    {
        const float y   = 1.3862944f / (halflife > 1.0e-5f ? halflife : 1.0e-5f);   ///< 2 ln 2 / halflife
        const float j0  = x - goal;
        const float j1  = v + j0 * y;
        const float eyd = std::exp(-y * dt);
        x = eyd * (j0 + j1 * dt) + goal;
        v = eyd * (v - j1 * y * dt);
    }

    /// @brief Orient turns the facing toward the movement; Strafe faces a target (the game's frame driver: the camera's
    ///        yaw while attacking, aiming, blocking or casting).
    enum class Mode : std::uint8_t { Orient, Strafe };

    /// @brief A character's movement numbers: what the step works with, per kind of character (the player, a horse).
    /// @note  The defaults are the player's: CMC's walking numbers as GASP's UpdateMovement_PreCMC sets them every tick,
    ///        converted to game units.
    struct Profile {
        float accel             = 560.0f;    ///< MaxAcceleration (800 cm/s^2), walk and run
        float sprintAccelSlow   = 210.0f;    ///< sprint: MaxAcceleration eases accel -> this ...
        float sprintAccelFrom   = 210.0f;    ///< ... as the speed goes from here (300 cm/s) ...
        float sprintAccelTo     = 490.0f;    ///< ... to here (700 cm/s)
        float friction          = 5.0f;      ///< GroundFriction (1/s), walk and run
        float sprintFrictionLow = 3.0f;      ///< sprint: GroundFriction eases friction -> this ...
        float sprintFrictionTo  = 350.0f;    ///< ... as the speed goes from 0 to here (500 cm/s)
        float brakingNoInput    = 1400.0f;   ///< BrakingDecelerationWalking without input (2000 cm/s^2)
        float brakingWithInput  = 350.0f;    ///< ... with input, i.e. above the cap after a gait drop (500 cm/s^2)
        /// @brief BrakingFrictionFactor: braking friction = ground friction x this (Unreal's default; GASP's class defaults
        ///        were not exported, so the default is assumed).
        float brakingFrictionFactor = 2.0f;

        float facingHalflife = 0.1f;          ///< GASP's Offset Root Bone rotation halflife, now the facing's
        float airTurnRate    = 3.4906585f;    ///< 200 deg/s: GASP's falling RotationRate, the facing's cap in the air
        float facingMinSpeed = 10.0f;         ///< below this speed the facing holds (no direction to chase)
        float sprintCone     = 0.87266463f;   ///< 50 deg: strafing sprints only within this of the facing (CanSprint)

        /// @brief Traction (the horse's dig-in turns, spec H5); every value 0 = off, as for the player.
        /// @note  lateralGrip caps the velocity's sideways change at this many units/s^2, so the turn radius at speed v is
        ///        v^2 / lateralGrip, never below minTurnRadius; brakingGrip caps its slowing the same way (a sliding stop),
        ///        CMC's included. Past digInAngle between the stick and the velocity, braking ramps from 0 up to
        ///        digInBraking at 180 deg (on top of CMC's own), and the facing turns toward the stick with
        ///        digInFacingHalflife, at most digInMaxLead ahead of the velocity, while the velocity lags (the body leads
        ///        the momentum); once slower than facingMinSpeed it turns the rest on the spot. Acceleration out is the
        ///        ordinary one.
        /// @note  Why brakingGrip: CMC's friction swings the velocity toward a stick straight back by braking it through
        ///        zero, which at a horse's friction stops a canter in ~0.2 s (~3 g); held to a sliding stop, the dig-in
        ///        slides and spirals instead.
        float lateralGrip         = 0.0f;   ///< units/s^2
        float brakingGrip         = 0.0f;   ///< units/s^2
        float minTurnRadius       = 0.0f;   ///< game units
        float digInAngle          = 0.0f;   ///< radians
        float digInBraking        = 0.0f;   ///< units/s^2 at a 180 deg request
        float digInFacingHalflife = 0.0f;   ///< seconds
        float digInMaxLead        = 0.0f;   ///< radians; 0 = no cap
    };
    /// @brief The player's profile.
    inline constexpr Profile kPlayer{};

    /// @brief CMC's own constants: the algorithm's, not a character's.
    constexpr float kBrakingSubstep   = 1.0f / 33.0f;   ///< CMC's BrakingSubStepTime
    constexpr float kBrakeToStop      = 7.0f;           ///< BRAKE_TO_STOP_VELOCITY (10 cm/s)
    constexpr float kOverMaxTolerance = 1.01f;          ///< IsExceedingMaxSpeed's 1% slack

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

    /// @brief One step of `dt` seconds for a character at (px, py), with the numbers of `profile`.
    /// @note  On the ground, CMC's CalcVelocity: without input, or above the cap, braking (substepped, friction x
    ///        brakingFrictionFactor, brakingNoInput or brakingWithInput), never braking below the cap while still
    ///        pushing forward; otherwise friction swings the velocity toward the stick at its current speed; then the
    ///        stick's acceleration, clamped to the cap; then the profile's traction, when it has one (grip, dig-in
    ///        braking). Airborne leaves the velocity alone. Then the facing: a spring (facingHalflife) toward the
    ///        velocity's heading (strafe: toward the target from (px, py); digging in: toward the stick, with
    ///        digInFacingHalflife), its rate capped at airTurnRate while airborne; it holds below facingMinSpeed.
    void Step(State& s, const Input& in, const Caps& caps, const Profile& profile, float px, float py, float dt);

    /// @brief Whether the direction allows sprinting: always when orienting to movement; when strafing, only while the
    ///        stick is within the profile's sprintCone of the facing (GASP's CanSprint).
    bool SprintDirectionAllowed(const State& s, const Input& in, const Profile& profile);

}  // namespace CB::engine::locomotion
