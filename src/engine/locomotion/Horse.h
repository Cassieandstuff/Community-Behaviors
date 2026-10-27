#pragma once

#include <locomotion/Movement.h>

#include <cstdint>

/// @file
/// @brief The ridden horse's character: its movement numbers and its gait ladder (spec H3, H6, H7). The game reads
///        the rider's toggles and the horse's MOVT speeds; the ladder's choices are here.

namespace CB::engine::locomotion {

    /// @brief The ridden horse's model numbers and its gait ladder.
    /// @note  Starting values, to be hand-tuned against the horse's acceptance scenarios (spec H3): acceleration is
    ///        vanilla's ridden-horse rate (2 x the canter speed: 0.5 s to canter), the air turn cap Horse_Fall_MT's
    ///        rotate-while-moving (45 deg/s); the horse has no sprint ramps (accel and friction hold through a gallop).
    ///        Traction (spec H5) from rough real-world estimates, unverified, at 70 units per metre and g = 687
    ///        units/s^2: grip 0.7 g sideways (turn radius ~6 m at canter, ~11 m at gallop) and 0.6 g braking (a
    ///        sliding stop), a 1.5 m floor (turning on the hocks), dig-in past 60 deg braking up to that sliding stop
    ///        at 180 deg, the body leading with 0.08 s by at most 60 deg.
    struct HorseProfile {
        Profile model{
            .accel               = 900.0f,
            .sprintAccelSlow     = 900.0f,
            .sprintAccelFrom     = 0.0f,
            .sprintAccelTo       = 1.0f,
            .friction            = 3.0f,
            .sprintFrictionLow   = 3.0f,
            .sprintFrictionTo    = 1.0f,
            .brakingNoInput      = 700.0f,
            .brakingWithInput    = 350.0f,
            .facingHalflife      = 0.15f,
            .airTurnRate         = 0.7853982f,
            .lateralGrip         = 480.0f,
            .brakingGrip         = 412.0f,
            .minTurnRadius       = 105.0f,
            .digInAngle          = 1.0471976f,
            .digInBraking        = 412.0f,
            .digInFacingHalflife = 0.08f,
            .digInMaxLead        = 1.0471976f,
        };
        /// @brief Trot's speed as a fraction of the way from walk to canter (vanilla has no trot speed).
        float trotFraction = 0.4f;
        /// @brief Gamepad deflection below which the top rung is walk, then trot; at or above trotBelow it is canter.
        float walkBelow = 0.45f;
        float trotBelow = 0.85f;
    };
    /// @brief The ridden horse's profile.
    inline constexpr HorseProfile kHorse{};

    /// @brief The gait ladder's rungs (spec H6): holding forward climbs to canter; the sprint toggle is gallop.
    enum class Rung : std::uint8_t { Walk, Trot, Canter, Gallop };

    /// @brief The highest rung the rider allows: gallop while the horse sprints (vanilla's sprint toggle), walk while
    ///        not running, else the stick's deflection picks it (spec H7; keyboard = full deflection).
    /// @param running vanilla's own test (the game: a gamepad always runs, else the run/walk toggle), so the walk toggle
    ///        caps keyboard riders and the deflection gamepad ones.
    inline Rung TopRung(bool sprinting, bool running, float deflection, const HorseProfile& horse = kHorse)
    {
        if (sprinting) return Rung::Gallop;
        if (!running || deflection < horse.walkBelow) return Rung::Walk;
        return deflection < horse.trotBelow ? Rung::Trot : Rung::Canter;
    }

    /// @brief The rung's speed from the horse's forward walk and run speeds: walk and canter are those; trot sits
    ///        trotFraction between them; gallop is `run` too, read by the caller from the sprint movement type the
    ///        graph switches to while sprinting.
    inline float RungSpeed(Rung rung, float walk, float run, const HorseProfile& horse = kHorse)
    {
        switch (rung) {
        case Rung::Walk: return walk;
        case Rung::Trot: return walk + (run - walk) * horse.trotFraction;
        default:         return run;
        }
    }

}  // namespace CB::engine::locomotion
