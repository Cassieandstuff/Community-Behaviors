#pragma once

#include <cmath>

/// @file
/// @brief CB's third-person camera rig: a fixed pipeline of stages, each shaped like a blueprint node (its profile's
///        parameters, its own state, typed inputs and an output), so a stage can later become a CB blueprint node
///        unchanged. Plain math: no game types.
/// @note Order per frame: Orientation → Anchor → (components' framing, eased: Mounted) → Boom → (the game casts the boom
///       against the world) → Collision → Probes → (a last sweep clamps the shift).
/// @note Units are game units and seconds; a halflife of 0 means "no smoothing".

namespace CB::feature::camera::rig {

    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    inline Vec3  operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3  operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3  operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    inline float Length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
    inline Vec3  Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

    /// @brief A rotation as its row-major matrix, laid out like Skyrim's NiMatrix3 (`m[row][column]`), applied as
    ///        world = M * local with the camera's local axes x right, y forward, z up — vanilla's third-person
    ///        offset math (0x1408e8640).
    struct Basis {
        float m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

        Vec3 Apply(Vec3 v) const
        {
            return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                     m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
        }
    };

    /// @brief The camera's own world yaw and pitch, turned by the look input (Skyrim's angles: yaw clockwise from +y,
    ///        pitch positive looking down).
    /// @note The rate is vanilla's free-look rate: look × speed × dt, look already shaped by the game's sensitivity
    ///       and inversion (PlayerControls lookInputVec; vanilla's fFreeRotationSpeed is 3).
    struct OrientationParams {
        float speed    = 3.0f;
        float maxPitch = 1.5707964f;  ///< vanilla's body pitch limit
    };
    struct OrientationState {
        float yaw   = 0.0f;
        float pitch = 0.0f;
    };
    struct OrientationIn {
        float lookX = 0.0f, lookY = 0.0f;
        float dt    = 0.0f;
    };
    void StepOrientation(const OrientationParams& p, OrientationState& s, const OrientationIn& in);

    /// @brief The point the camera orbits, following its target with separate horizontal and vertical lag.
    /// @note Why split: vertical lag smooths stairs, landings and the Camera3rd bone's steps (sneak) without letting
    ///       the player drift sideways out of frame.
    struct AnchorParams {
        float halflifeXY = 0.0f;
        float halflifeZ  = 0.06f;
    };
    struct AnchorState {
        bool init = false;
        Vec3 pos;
    };
    struct AnchorIn {
        Vec3  target;
        float dt = 0.0f;
    };
    Vec3 StepAnchor(const AnchorParams& p, AnchorState& s, const AnchorIn& in);

    /// @brief The arm from the anchor to the eye: the shoulder offset plus the zoom distance behind it, in the
    ///        camera's frame.
    /// @note Vanilla's numbers: the distance is lerp(minDistance, maxDistance, zoom) plus the pitch zoom, taken off
    ///       the offset's forward axis (fVanityModeMinDist / fVanityModeMaxDist, ThirdPersonState pitchZoomOffset).
    struct BoomParams {
        float minDistance = 155.0f;
        float maxDistance = 600.0f;
    };
    struct BoomIn {
        Vec3  anchor;
        Vec3  shoulder;       ///< camera frame (x right, y forward, z up)
        float zoom      = 0;  ///< vanilla's current zoom: 0 = nearest, 1 = farthest, below 0 heading to first person
        float pitchZoom = 0;  ///< extra distance from looking down or up
        Basis rotation;
    };
    struct BoomOut {
        Vec3 pivot;  ///< the anchor plus the shoulder's sideways and vertical part: where the boom starts
        Vec3 eye;    ///< where the camera wants to be
    };
    BoomOut StepBoom(const BoomParams& p, const BoomIn& in);

    /// @brief Keeps the eye in front of whatever the boom's sphere hit: in at once, held briefly, back out over a
    ///        halflife. A thin object only ever counts when the eye would end up in it, and eases in, so orbiting past a
    ///        trunk does not pop.
    /// @note Vanilla's radius is 15 (ApplyCameraCaster 0x1408e48d0).
    struct CollisionParams {
        float radius          = 15.0f;
        float recoverHalflife = 0.15f;
        float holdTime        = 0.15f;  ///< stays pulled in this long, so clutter does not pump the boom
        bool  passThin        = true;   ///< thin objects leave the boom alone; off = they block like walls
        float thinWidth       = 40.0f;  ///< thin = the second-largest extent of its bounds is below this
        float thinInHalflife  = 0.05f;
        float softInHalflife  = 0.12f;  ///< how the boom gives way to a surface just behind the eye
    };
    struct CollisionState {
        bool  init     = false;
        float distance = 0.0f;  ///< the boom length last shown
        float hold     = 0.0f;  ///< seconds left before it may ease back out
    };
    struct CollisionIn {
        Vec3  pivot;
        Vec3  eye;
        float hitFraction = 1.0f;  ///< along pivot → eye; 1 = clear
        bool  thin        = false;  ///< what it hit is a thin object
        float softPull    = 0.0f;   ///< clear, but this much shorter for a surface just beyond the eye
        float dt          = 0.0f;
    };
    Vec3 StepCollision(const CollisionParams& p, CollisionState& s, const CollisionIn& in);

    /// @brief Four short probes around the camera: surfaces near it push it away on a critically damped spring (a PD
    ///        controller, no integral term to wind up against a wall), so it slows and slides before it touches.
    /// @note The push is squared closeness, so it fades to nothing at the probe's reach and a far hit cannot flicker.
    ///       Opposite probes subtract, which centres the camera in a corridor. The Collision sweep still has the last
    ///       word: a spring can be outrun.
    struct ProbeParams {
        bool  enabled  = true;
        float reach    = 60.0f;
        float radius   = 5.0f;
        float maxShift = 30.0f;  ///< caps how far the player drifts in frame
        float boomPull = 30.0f;  ///< how much the boom shortens for a surface right behind the eye
        EaseParams spring{ 0.25f };
    };
    struct ProbeState {
        EaseState x;
        EaseState z;
    };
    /// @note Fractions along each probe; 1 = clear.
    struct ProbeIn {
        float left = 1.0f, right = 1.0f, down = 1.0f, up = 1.0f;
        float dt   = 0.0f;
    };
    /// @return The shift in the camera's frame (x right, z up).
    Vec3 StepProbes(const ProbeParams& p, ProbeState& s, const ProbeIn& in);

    /// @brief The share of the boom pull for a surface `beyond` units past the eye.
    float SoftPull(const ProbeParams& p, float beyond);

    /// @brief A 0..1 weight eased toward its target on a critically damped spring: how far the camera has moved into
    ///        a component's framing.
    /// @note Critically damped so it arrives without overshoot; `settle` is roughly the time it takes (the SmoothDamp
    ///       of Game Programming Gems 4, as in Unity's Mathf.SmoothDamp).
    struct EaseParams {
        float settle = 0.5f;
    };
    struct EaseState {
        float value = 0.0f;
        float rate  = 0.0f;
    };
    float StepEase(const EaseParams& p, EaseState& s, float target, float dt);

    /// @brief The Mounted component: while the player rides, the pivot moves to the mount's (its root plus its camera
    ///        height) and the boom's shoulder to the mount's offset — vanilla's mounted framing (HorseCameraState: the
    ///        mount race's camera offset sheathed, the mounted combat offset drawn).
    struct MountedParams {
        EaseParams ease;
        float      shoulderHalflife = 0.2f;  ///< smooths the mount offset's jump on drawing or sheathing
        float      height           = 0.0f;  ///< added to the mount's camera height
        EaseParams body{ 0.3f };             ///< the rider's pivot onto the body for the mount and dismount clips and off it
        float      comHalflife      = 0.5f;  ///< smooths the on-foot measure of the body's offset from the root
    };

    /// @brief One named set of every stage's parameters.
    struct Profile {
        const char*       name = "Default";
        OrientationParams orientation;
        AnchorParams      anchor;
        BoomParams        boom;
        CollisionParams   collision;
        ProbeParams       probes;
        MountedParams     mounted;
    };

    /// @note Orientation is not here: it survives a reset of the rest (re-entering third person keeps the view).
    struct State {
        AnchorState    anchor;
        CollisionState shoulder;   ///< the anchor → pivot leg
        CollisionState collision;  ///< the pivot → eye leg
        ProbeState     probes;
    };

    /// @brief `current` moved toward `target` by the share a halflife leaves after `dt`.
    inline float Damp(float current, float target, float halflife, float dt)
    {
        if (halflife <= 0.0f || dt <= 0.0f) return target;
        return target + (current - target) * std::exp2(-dt / halflife);
    }

}  // namespace CB::feature::camera::rig
