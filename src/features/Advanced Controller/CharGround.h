#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>

#include <array>
#include <cmath>
#include <string_view>

/// @file
/// @brief Shared pieces of the character controller spec's decision layer: which controllers CB manages, the live
///        hull, the M4 floor query (a ray footprint) and the exact critically damped spring (P2). Every character
///        feature uses these instead of its own copy.

namespace CB::feature::controller {

    namespace hk = CB::havok;

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

    /// @brief Whether ground is under the feet at all. How much it holds the character up is a separate axis, the
    ///        verdict's support: a steep slope is Grounded with low support.
    enum class Ground { Airborne, Grounded };
    const char* Name(Ground g);

    struct FloorHit {
        bool                     hit = false;
        float                    dz  = 0;   ///< hit height above the feet (Havok units)
        float                    nx = 0, ny = 0, nz = 0;
        unsigned                 layer = 0;
        const hk::hkpCollidable* body = nullptr;
    };

    /// @brief The support at and above which the ground holds the character (Supported): the constraint planes treat its
    ///        contacts as support, it gives the support body, and the step query may step onto it.
    /// @note Landing, jumping and the coyote window need only Grounded; support scales the slide and the uphill clip
    ///       instead of gating them.
    constexpr float kSupportedAt = 0.5f;

    /// @brief The M4 floor query's verdict.
    struct FloorVerdict {
        Ground                   ground    = Ground::Airborne;
        float                    support   = 0;       ///< 0-1 from the averaged normal's slope (Support); 0 when Airborne
        bool                     stickable = false;   ///< a floor-like hit (walls and characters excluded) within probe depth below the feet (M8)
        float                    stickDz   = 0;       ///< the highest such hit below the foot tolerance (negative)
        float                    groundNx = 0, groundNy = 0, groundNz = 1;   ///< average normal of every support hit (Support reads it)
        const hk::hkpCollidable* supportBody = nullptr;   ///< a support hit's body (a walkable one first)
        int                      supportHits = 0, walkableHits = 0;
        std::array<FloorHit, 9>  hits{};   ///< axis, then the ring at the perch radius
    };

    /// @brief Ground that holds the character up: Grounded with support at or above kSupportedAt.
    inline bool Supported(const FloorVerdict& v) { return v.ground == Ground::Grounded && v.support >= kSupportedAt; }

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

    /// @brief How much a ground of normal z `groundNz` holds the character up, 0-1: 1 up to [Character] fSlidePullStart
    ///        degrees (default 5) below the controller's max slope (acos m_zNormal), easing to 0 at fSlidePullFull
    ///        degrees (default 10) past it.
    /// @note Why a number and not a threshold: a hard walkable/too-steep line gave two movement models a degree apart
    ///       (and needed a hysteresis delay on rough ground); the slide below scales with it instead.
    float Support(const hk::bhkCharacterController& ctrl, float groundNz);

    /// @brief The slide: a horizontal velocity the player does not control, added to the player's own on the ground.
    ///        It gains gravity's pull along the ground scaled by 1 - support, bleeds off at a rate scaled by support,
    ///        and never exceeds kMaxSlideSpeed.
    /// @param gx gravity (Havok units/s^2, already scaled by the controller's m_gravity).
    /// @note The gain is horizontal at gravity's along-slope magnitude because the ground step lays the horizontal
    ///       velocity onto the plane at the same speed, so the body accelerates down the fall line at g sin(slope)
    ///       times 1 - support.
    void SlideStep(const FloorVerdict& v, float gx, float gy, float gz, float& sx, float& sy, float dt);

    /// @brief How far the ground of `v` rises along a horizontal move (dx, dy) if it keeps its plane (Havok units); with a
    ///        velocity, the vertical speed that following the ground gives.
    /// @note Clamped to a 60 deg grade: a near-vertical normal would extrapolate the move into a cliff. 0 when Airborne.
    inline float SlopeRise(const FloorVerdict& v, float dx, float dy)
    {
        if (v.ground == Ground::Airborne || v.groundNz < 0.1f) return 0.0f;
        const float rise  = -(v.groundNx * dx + v.groundNy * dy) / v.groundNz;
        const float limit = 1.7320508f * std::sqrt(dx * dx + dy * dy);
        return rise < -limit ? -limit : rise > limit ? limit : rise;
    }

    /// @brief Removes 1 - support of the uphill part of a horizontal request: on ground that does not hold the
    ///        character, the player can steer across and down it but not climb it (Jolt's CharacterVirtual sample
    ///        does the same on steep ground).
    void ClipUphill(const FloorVerdict& v, float& dx, float& dy);

    /// @brief What the ground states hand the predictor each step.
    struct GroundNote {
        float slideX = 0, slideY = 0;   ///< the slide velocity (Havok units/s, horizontal)
        float jumpIn    = -1;           ///< seconds until a pending ground jump launches (its wind-up), -1 for none
        float jumpSpeed = 0;            ///< that launch's vertical speed (Havok units/s) from the ground now under the feet
    };
    void PublishGroundNote(const hk::bhkCharacterController& ctrl, const GroundNote& note);
    /// @brief The note last published for `ctrl`; false when none is.
    bool PublishedGroundNote(const hk::bhkCharacterController& ctrl, GroundNote& out);

    /// @brief The movement the character asks for as a world-space horizontal velocity (Havok units/s).
    /// @note Through vanilla's calculateMovement with the ground flat, a zero current velocity and an unlimited delta:
    ///       exactly the frame OnGround steers in (m_velocityMod's x/y swapped, m_forward), with no sign convention of
    ///       ours to get wrong.
    void DesiredHorizontal(const hk::bhkCharacterController& ctrl, float& dx, float& dy);

    /// @brief The world directions DesiredHorizontal moves a pure forward request (m_velocityMod y) and a pure
    ///        sideways one (m_velocityMod x) along, unit length.
    /// @note Asked of calculateMovement itself rather than rebuilt from m_forward: a hand derivation of its cross
    ///       products once got the side axis's sign wrong, which mirrored every sideways prediction (invisible while
    ///       True Directional Movement kept the character facing its movement, so the side request was always zero).
    /// @return false when the frame is degenerate (m_forward vertical or zero).
    bool MovementAxes(const hk::bhkCharacterController& ctrl, float& forwardX, float& forwardY, float& sideX, float& sideY);

    /// @brief settings.ini `[Character] key` as a float, `def` when absent or unparsable (callers cache it).
    float SettingFloat(std::string_view key, float def);
    /// @brief The game's INI setting `name` (e.g. "fInAirFallingCharGravityMult:HAVOK") as a float, `def` when absent.
    float IniFloat(const char* name, float def);

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

}  // namespace CB::feature::controller
