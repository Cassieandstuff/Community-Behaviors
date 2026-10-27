#pragma once

#include <vector>

/// @file
/// @brief Dead blending (Holden): on a switch of the playing source, the pose being shown keeps moving at its own last
///        velocity, slowing with a decay halflife, and the output crosses from that coasting pose into the live source's
///        over the blend time on a smoothstep. It needs only the blend's own past outputs, not the new source's
///        velocity, which the caller never has to supply. A switch mid-blend starts from the pose then shown, so
///        switches never pop.

namespace CB::engine::locomotion {

    struct Quat {
        float x = 0, y = 0, z = 0, w = 1;
    };
    /// @brief One bone of a local pose: translation, rotation, scale.
    /// @invariant Laid out as Havok's hkQsTransform (48 bytes; the w lanes of t and s are carried through untouched), so
    ///            a caller holding Havok poses copies them in and out bytewise.
    struct Bone {
        float t[4] = {};
        Quat  q;
        float s[4] = { 1, 1, 1, 0 };
    };
    static_assert(sizeof(Bone) == 48, "Bone mirrors hkQsTransform");
    /// @brief A bone's velocity: linear (pose units/s) and angular (rad/s, scaled axis, parent space).
    struct BoneVelocity {
        float t[3] = {}, w[3] = {};
    };

    /// @brief The blend's numbers (the game: settings.ini [Character] fMMBlendTime, fMMBlendDecay).
    struct DeadBlendTuning {
        float blendTime = 0.2f;   ///< seconds the cross-fade takes; 0 switches blending off
        float decay     = 0.1f;   ///< halflife (s) of the coasting pose's velocity
    };

    /// @brief Caps on a coasting bone's velocity, so one bad frame cannot fling it.
    constexpr float kMaxAngular = 20.0f;     ///< rad/s
    constexpr float kMaxLinear  = 2000.0f;   ///< pose units/s
    /// @brief A blended bone further than this from the live source's (pose units) is taken as broken.
    constexpr float kMaxBlendOffset = 500.0f;

    /// @brief Whether `b`'s translation (xyz) and rotation are finite.
    bool FiniteBone(const Bone& b);

    /// @brief The first blended bone Step found broken (non-finite, or past kMaxBlendOffset) while the live bone was
    ///        finite, and fell back to the live one.
    struct BlendFault {
        int   bone   = -1;   ///< -1: none
        Bone  blended;
        Bone  live;
        float offset = 0.0f;   ///< the blended bone's distance from the live one
    };

    /// @brief One character's dead-blend state: its last two output poses, and the pose and velocity frozen at a switch.
    struct DeadBlend {
        int                       bones   = 0;
        int                       child   = -1;            ///< the source the last output played
        int                       history = 0;             ///< outputs recorded since the last reset, up to 2
        float                     dt      = 1.0f / 60.0f;  ///< the latest update's timestep (the caller sets it)
        bool                      blending = false;
        float                     t        = 0.0f;         ///< seconds into the blend
        std::vector<Bone>         last, prev, from;
        std::vector<BoneVelocity> vel;

        void Reset(int n, int playing);
        void Record(const Bone* pose);
        /// @brief Freeze the pose last shown and its velocity (from the last two outputs) as the blend's source.
        void Start();

        /// @brief One output: `pose` (`n` bones, the live source `playing`'s) becomes the blended pose, and is recorded.
        /// @return false when `n` changed: the state reset to `n` bones and recorded `pose`, which it left as it was.
        /// @note  A switch (playing != child) starts a blend once two outputs are recorded and blendTime > 0; the
        ///        blend advances by dt. A broken blended bone falls back to the live one (`fault`, when given, names the
        ///        first whose live bone was finite).
        bool Step(Bone* pose, int n, int playing, const DeadBlendTuning& tuning, BlendFault* fault = nullptr);

        /// @brief Seconds left in the blend, 0 when none runs.
        float Remaining(const DeadBlendTuning& tuning) const { return blending ? tuning.blendTime - t : 0.0f; }
    };

}  // namespace CB::engine::locomotion
