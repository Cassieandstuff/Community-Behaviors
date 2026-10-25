#pragma once

#include <cmath>

/// @file
/// @brief CB's third-person camera rig: a fixed pipeline of stages, each shaped like a blueprint node (its profile's
///        parameters, its own state, typed inputs and an output), so a stage can later become a CB blueprint node
///        unchanged. Plain math: no game types.
/// @note Order per frame: Orientation → Anchor → Boom → (the game casts the boom against the world) → Collision.
/// @note Units are game units and seconds; a halflife of 0 means "no smoothing".

namespace CB::feature::camera::rig {

    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    inline Vec3  operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3  operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3  operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    inline float Length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

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

    /// @brief Keeps the eye in front of whatever the boom hit: in at once, back out over a halflife.
    struct CollisionParams {
        float radius          = 10.0f;  ///< stays this far short of a hit
        float recoverHalflife = 0.15f;
    };
    struct CollisionState {
        bool  init     = false;
        float distance = 0.0f;  ///< the boom length last shown
    };
    struct CollisionIn {
        Vec3  pivot;
        Vec3  eye;
        float hitFraction = 1.0f;  ///< along pivot → eye; 1 = clear
        float dt          = 0.0f;
    };
    Vec3 StepCollision(const CollisionParams& p, CollisionState& s, const CollisionIn& in);

    /// @brief One named set of every stage's parameters.
    struct Profile {
        const char*       name = "Default";
        OrientationParams orientation;
        AnchorParams      anchor;
        BoomParams      boom;
        CollisionParams collision;
    };

    /// @note Orientation is not here: it survives a reset of the rest (re-entering third person keeps the view).
    struct State {
        AnchorState    anchor;
        CollisionState collision;
    };

    /// @brief `current` moved toward `target` by the share a halflife leaves after `dt`.
    inline float Damp(float current, float target, float halflife, float dt)
    {
        if (halflife <= 0.0f || dt <= 0.0f) return target;
        return target + (current - target) * std::exp2(-dt / halflife);
    }

}  // namespace CB::feature::camera::rig
