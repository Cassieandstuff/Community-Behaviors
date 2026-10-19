#pragma once

#include <feature-sdk/havok/Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>

#include <array>
#include <cmath>
#include <string_view>

/// @file
/// @brief Shared pieces of the character controller spec's decision layer: which controllers CB manages, the live
///        hull, the M4 floor query (a ray footprint) and the exact critically damped spring (P2). Every character
///        feature uses these instead of its own copy.

namespace CB::feature::character {

    namespace hk = CB::schema;

    constexpr hk::hkUint32 kSupport                     = 1u << 8;    ///< m_flags: UpdatePhysics' verdict
    constexpr hk::hkUint32 kHasPotentialSupportManifold = 1u << 9;
    constexpr hk::hkUint32 kCanJump                     = 1u << 10;
    constexpr hk::hkUint32 kJumping                     = 1u << 13;
    constexpr unsigned     kCharControllerLayer         = 30;

    /// @brief Whether `ctrl` is the player's controller.
    bool IsPlayer(const hk::bhkCharacterController* ctrl);
    /// @brief Whether `ctrl` is the bhkCharacterController base of a bhkCharProxyController.
    bool IsProxy(const hk::bhkCharacterController& ctrl);
    /// @brief The controllers CB's decision layer runs: the player's proxy controller (spec scope: player first).
    inline bool IsManaged(const hk::bhkCharacterController& ctrl) { return IsPlayer(&ctrl) && IsProxy(ctrl); }

    /// @brief The live hull in Havok world space, read from the phantom's shape AABB (so DCA's reshapes are seen).
    struct Hull {
        bool                     valid  = false;
        hk::bhkWorld*            world  = nullptr;
        const hk::hkpCollidable* collidable = nullptr;   ///< the phantom's; Sweep() moves this shape
        float                    feetZ = 0, cx = 0, cy = 0, radius = 0, topZ = 0;
    };
    /// @pre `ctrl` is a proxy controller (IsProxy).
    Hull GetHull(hk::bhkCharacterController& ctrl);

    enum class Ground { Airborne, Grounded, Sliding };
    const char* Name(Ground g);

    struct FloorHit {
        bool                     hit = false;
        float                    dz  = 0;   ///< hit height above the feet (Havok units)
        float                    nx = 0, ny = 0, nz = 0;
        unsigned                 layer = 0;
        const hk::hkpCollidable* body = nullptr;
    };

    /// @brief The M4 floor query's verdict.
    struct FloorVerdict {
        Ground                   ground    = Ground::Airborne;
        bool                     stickable = false;   ///< a walkable hit within probe depth below the feet (M8)
        float                    stickDz   = 0;       ///< the highest walkable hit below the foot tolerance (negative)
        float                    groundNx = 0, groundNy = 0, groundNz = 1;   ///< average normal of every support hit; Grounded when walkable
        const hk::hkpCollidable* supportBody = nullptr;   ///< a support hit's body (a walkable one first)
        int                      supportHits = 0, walkableHits = 0;
        std::array<FloorHit, 9>  hits{};   ///< axis, then the ring at the perch radius
    };

    /// @brief Tunables (Havok units); the spec's scenarios set them.
    struct FloorTunables {
        float perchFraction = 0.6f;   ///< perch radius as a fraction of the hull radius
        float footTolerance = 0.1f;   ///< about 7 game units
        float wallNormalZ   = 0.3f;   ///< faces steeper than about 72 deg are walls, never ground
    };

    /// @brief Runs the floor query: nine bhkWorld::PickObject rays from one hull radius above the feet down to the step
    ///        height below them, with the controller's own collision filter; characters and walls never count.
    /// @pre Called where vanilla OnGround::Simulate calls PickObject: inside UpdatePhysics, outside the physics step.
    FloorVerdict QueryFloor(hk::bhkCharacterController& ctrl, const Hull& hull, const FloorTunables& t = {});
    /// @brief Where the floor query casts ray `i` (0 = the axis, 1-8 = the ring at the perch radius), Havok units.
    void FloorRayXY(const Hull& hull, int i, const FloorTunables& t, float& x, float& y);
    /// @brief QueryFloor as if the hull stood displaced by (dx, dy, dz) (a step's landing spot, a predicted sample).
    FloorVerdict QueryFloorAt(hk::bhkCharacterController& ctrl, const Hull& hull, float dx, float dy, float dz, const FloorTunables& t = {});

    /// @brief Publishes this step's floor verdict for the proxy's constraint planes (step 4) to read during integrate.
    /// @note The planes cannot query themselves: integrate runs under the world WRITE lock, where a ray pick may
    ///       deadlock. Two publishers, latest wins: CheckSupportImpl (the raw query) and the ground states' Simulate
    ///       (their decided mode). Simulate is the one that runs every step: UpdatePhysics skips CheckSupportImpl while
    ///       an OnGround character stands still, which left the planes on a stale Grounded verdict, its anti-creep
    ///       friction holding a steep slope's downhill motion.
    void PublishVerdict(const hk::bhkCharacterController& ctrl, const FloorVerdict& v);
    /// @brief The verdict last published for `ctrl`; false when none is.
    bool PublishedVerdict(const hk::bhkCharacterController& ctrl, FloorVerdict& out);

    /// @brief settings.ini `[Character] key` as a float, `def` when absent or unparsable (callers cache it).
    float SettingFloat(std::string_view key, float def);

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

}  // namespace CB::feature::character
