#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>

#include <locomotion/Movement.h>
#include <locomotion/Slope.h>

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

    /// @brief Publishes the horse the player rides (null: none), each frame its frame driver sees it ridden.
    /// @note The ground layer's side of the Ridden component (spec H12): physics callbacks ask IsRiddenHorse instead of
    ///       resolving the mount themselves (some run under the world's write lock).
    void PublishRiddenHorse(RE::Actor* horse);
    /// @brief Whether `ctrl` is the controller of the horse the player rides, as published within the last half second.
    bool IsRiddenHorse(const hk::bhkCharacterController* ctrl);
    /// @brief Whether a ridden horse is published (within the last half second).
    bool Riding();
    /// @brief Whether `ctrl` is the controller the player controls: the ridden horse's while Riding(), else the player's.
    /// @note The debug HUD shows this controller's step.
    inline bool IsControlled(const hk::bhkCharacterController* ctrl) { return Riding() ? IsRiddenHorse(ctrl) : IsPlayer(ctrl); }

    /// @brief The controllers CB's decision layer runs: the player's and the ridden horse's, on the proxy.
    inline bool IsManaged(const hk::bhkCharacterController& ctrl) { return IsProxy(ctrl) && (IsPlayer(&ctrl) || IsRiddenHorse(&ctrl)); }
    /// @brief The actor a managed controller moves (the player, the ridden horse); null for any other.
    RE::NiPointer<RE::Actor> ManagedActor(const hk::bhkCharacterController& ctrl);

    /// @brief The graph flag under which the graph moves `actor` itself (bAnimationDriven, bIsSynced), empty when
    ///        neither is set: then every CB layer that steers the actor hands it to vanilla.
    /// @note Why: a synced clip (a mount, a paired idle, furniture) moves its support character onto the scene's mark
    ///       and advances only once the character stands within the mark's threshold (BSSynchronizedClipGenerator
    ///       update), so a velocity of CB's in its place holds the character off the mark and the clip never advances.
    /// @note For a managed actor it also publishes the answer for GraphMovesPublished.
    const char* GraphMovesActor(RE::Actor* actor);
    /// @brief The GraphMovesActor answer for the actor of managed controller `ctrl` as of its last call (false before any).
    /// @note For the proxy's constraint planes: integrate runs under the world's write lock (see PublishVerdict), so they
    ///       read this instead of the graph.
    bool GraphMovesPublished(const hk::bhkCharacterController& ctrl);

    /// @brief The steepest face (as its normal's z) the managed controller walks on: the controller's own m_zNormal
    ///        (vanilla's 47 deg) for the player; the horse profile's steeper angle for the ridden horse (spec S12).
    float WalkableZ(const hk::bhkCharacterController& ctrl);

    /// @brief The live hull in Havok world space, read from the phantom's shape AABB (so DCA's reshapes are seen).
    /// @note A long body (a horse: the engine builds it from capsules along its length, bhkCharacterControllerCinfo
    ///       0x140e9cdf0) is read from the shape's own box and the phantom's rotation, which the controller turns with
    ///       the actor (SetProxyOrientation 0x140ea06e0 -> RotateImpl): a world box would square off under a turn.
    struct Hull {
        bool                     valid  = false;
        hk::bhkWorld*            world  = nullptr;
        const hk::hkpCollidable* collidable = nullptr;   ///< the phantom's; Sweep() moves this shape
        float                    feetZ = 0, cx = 0, cy = 0, radius = 0, topZ = 0;   ///< radius: half the width for a long body
        bool                     longBody   = false;
        float                    fx = 0, fy = 1;          ///< a long body's length axis, horizontal, unit length
        float                    halfLength = 0;          ///< half a long body's length
    };
    /// @pre `ctrl` is a proxy controller (IsProxy).
    Hull GetHull(hk::bhkCharacterController& ctrl);

    /// @brief The squared horizontal distance from (x, y) to the hull's supporting axis: its centre, or for a long body
    ///        the segment between its two ends' centres (Havok units squared).
    inline float SupportAxisDistSq(const Hull& hull, float x, float y)
    {
        float ax = hull.cx, ay = hull.cy;
        if (hull.longBody) {
            const float reach = hull.halfLength > hull.radius ? hull.halfLength - hull.radius : 0.0f;
            float       along = (x - hull.cx) * hull.fx + (y - hull.cy) * hull.fy;
            along             = along < -reach ? -reach : along > reach ? reach : along;
            ax += hull.fx * along, ay += hull.fy * along;
        }
        const float dx = x - ax, dy = y - ay;
        return dx * dx + dy * dy;
    }

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
        std::array<FloorHit, 9>  hits{};       ///< axis, then the ring at the perch radius (a long body's fore end)
        bool                     longBody = false;
        std::array<FloorHit, 9>  hindHits{};   ///< a long body's hind end, as `hits`
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
    /// @note  A long body runs it twice, under its fore and hind ends (spec H10), and combines them: Grounded with the
    ///        lower support when both ends are; when one end is, Grounded on that end alone (it stands, and moving on
    ///        steps the other end down); Airborne when neither is. Its hits are the fore end's.
    /// @pre Called where vanilla OnGround::Simulate calls PickObject: inside UpdatePhysics, outside the physics step.
    FloorVerdict QueryFloor(hk::bhkCharacterController& ctrl, const Hull& hull, const FloorTunables& t = {});
    /// @brief Where the floor query casts ray `i` (0 = the axis, 1-8 = the ring at the perch radius), Havok units; for a
    ///        long body, under its fore end, or its hind end with `hind`.
    void FloorRayXY(const Hull& hull, int i, const FloorTunables& t, float& x, float& y, bool hind = false);
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
    ///        degrees (default 5) below the controller's max slope (acos WalkableZ), easing to 0 at fSlidePullFull
    ///        degrees (default 10) past it.
    /// @note Why a number and not a threshold: a hard walkable/too-steep line gave two movement models a degree apart
    ///       (and needed a hysteresis delay on rough ground); the slide below scales with it instead.
    float Support(const hk::bhkCharacterController& ctrl, float groundNz);

    /// @brief The ground of `v` as the engine's slope helpers read it.
    inline CB::engine::locomotion::Surface SurfaceOf(const FloorVerdict& v)
    {
        return { v.ground == Ground::Grounded, v.support, v.groundNx, v.groundNy, v.groundNz };
    }

    /// @brief locomotion::SlideStep on the ground of `v`.
    inline void SlideStep(const FloorVerdict& v, float gx, float gy, float gz, float& sx, float& sy, float dt)
    {
        CB::engine::locomotion::SlideStep(SurfaceOf(v), gx, gy, gz, sx, sy, dt);
    }

    /// @brief locomotion::SlopeRise on the ground of `v`.
    inline float SlopeRise(const FloorVerdict& v, float dx, float dy) { return CB::engine::locomotion::SlopeRise(SurfaceOf(v), dx, dy); }

    using CB::engine::locomotion::kSlopeSpeedHalflife;

    /// @brief locomotion::SlopeSpeedScale on the ground of `g`, with settings.ini [Character] fSlopeSpeedDown and
    ///        fSlopeSpeedUp (defaults 1.0 and 0.5), read once.
    float SlopeSpeedScale(const FloorVerdict& g, float dirX, float dirY);

    /// @brief The verdict last published for the player's controller; false when none is. For readers outside the
    ///        physics step (the player's frame movement).
    bool LatestVerdict(FloorVerdict& out);

    /// @brief locomotion::JumpLaunch from the ground of `g`.
    inline void JumpLaunch(const FloorVerdict& g, float hx, float hy, float speed, float& ox, float& oy, float& oz)
    {
        CB::engine::locomotion::JumpLaunch(SurfaceOf(g), hx, hy, speed, ox, oy, oz);
    }

    /// @brief locomotion::ClipUphill on the ground of `v`.
    inline void ClipUphill(const FloorVerdict& v, float& dx, float& dy) { CB::engine::locomotion::ClipUphill(SurfaceOf(v), dx, dy); }

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

    using CB::engine::locomotion::SpringDamperExact;

}  // namespace CB::feature::controller
