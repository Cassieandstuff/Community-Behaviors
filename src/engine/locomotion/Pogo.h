#pragma once

#include <locomotion/Movement.h>

#include <cmath>

/// @file
/// @brief The ground control laws every caller that moves a character on the ground runs (the ground states' live
///        step, the predictor's substeps): the horizontal velocity spring, and the pogo, a spring that holds the feet on
///        the floor while the collision hull floats above it, so steps and drops are ridden instead of snapped.
/// @note  Havok units, z up.
/// @note  The pogo and its probe selection are adapted from Box3D's dynamic mover (samples/dynamic_mover.cpp,
///        src/pogo_joint.c, b3SpringDamper in include/box3d/math_functions.h): Copyright (c) 2026 Erin Catto, MIT
///        License (https://github.com/erincatto/box3d). Its force limits are left out: at Box3D's 100x the body's
///        weight they never bind on a kinematic hull, which nothing pushes.

namespace CB::engine::locomotion {

    /// @brief The ground velocity spring's halflife (seconds).
    constexpr float kGroundHalflife = 0.08f;

    /// @brief One step of the ground velocity spring: the horizontal velocity (vx, vy), rates (ax, ay), toward the
    ///        request (dx, dy).
    /// @invariant The one copy: the live ground step and the predictor both call it, so the predicted character
    ///            accelerates as the real one does.
    inline void GroundVelocityStep(float& vx, float& vy, float& ax, float& ay, float dx, float dy, float dt)
    {
        SpringDamperExact(vx, ax, dx, kGroundHalflife, dt);
        SpringDamperExact(vy, ay, dy, kGroundHalflife, dt);
    }

    /// @brief The pogo spring's numbers (Box3D's dynamic mover defaults).
    struct Pogo {
        float hertz   = 3.0f;
        float damping = 0.8f;   ///< ratio, 1 = critical
    };

    /// @brief One step of the pogo: advances its correction rate `rate` (Havok units/s, + = up) for feet `height` above
    ///        the floor it holds them to, and returns it.
    /// @note  b3SpringDamper, implicit so any dt is stable. The rate is a position correction: the caller moves the
    ///        feet by it this step but never keeps it as velocity (Box3D's relax pass drops it), or every stair would
    ///        launch the character.
    /// @param canPull false: the pogo only pushes up (Box3D's zero tension while jumping).
    inline float PogoStep(float& rate, float height, const Pogo& p, bool canPull, float dt)
    {
        if (!(dt > 0.0f) || !(p.hertz > 0.0f)) return rate = 0.0f;
        constexpr float kTwoPi = 6.2831853f;
        const float     omega  = kTwoPi * p.hertz;
        const float     oh     = omega * dt;
        rate                   = (rate - omega * oh * height) / (1.0f + 2.0f * p.damping * oh + oh * oh);
        if (!canPull && rate < 0.0f) rate = 0.0f;
        return rate;
    }

    /// @brief One downward probe ray under the hull.
    struct PogoProbe {
        bool  hit = false;
        bool  floorLike = false;   ///< not a wall or a character
        float dz = 0.0f;           ///< hit height above the feet
        float nz = 0.0f;           ///< the hit face's normal z
    };

    /// @brief Which probe the pogo holds the feet to; probe 0 is the hull's axis, the rest its skirt.
    /// @note  Box3D's skirt rule: the axis while it stands on walkable ground; else the walkable skirt probe that moves
    ///        the feet least (so the character stands on a ledge or a rail, or steps off the foot of a slope); with no
    ///        walkable probe, the axis's steep floor (the character slides off it), else the steep skirt probe nearest
    ///        the feet.
    /// @param which receives the chosen probe's index when not null.
    /// @return false when no probe met a floor.
    inline bool PogoSelect(const PogoProbe* probes, int count, float walkableZ, float& dz, int* which = nullptr)
    {
        if (count <= 0) return false;
        int  chosen = -1;
        auto take   = [&](int i) {
            chosen = i;
            dz     = probes[i].dz;
        };
        auto nearest = [&](bool walkableOnly) {
            for (int i = 1; i < count; i++) {
                const PogoProbe& p = probes[i];
                if (!p.hit || !p.floorLike || (walkableOnly && p.nz < walkableZ)) continue;
                if (chosen < 0 || std::fabs(p.dz) < std::fabs(dz)) take(i);
            }
            return chosen >= 0;
        };
        const PogoProbe& axis = probes[0];
        if (axis.hit && axis.floorLike && axis.nz >= walkableZ) take(0);
        else if (!nearest(true)) {
            if (axis.hit && axis.floorLike) take(0);
            else nearest(false);
        }
        if (which) *which = chosen;
        return chosen >= 0;
    }

}  // namespace CB::engine::locomotion
