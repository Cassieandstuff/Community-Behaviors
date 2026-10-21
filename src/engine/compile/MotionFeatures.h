#pragma once

#include "interface/AnimationDef.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

/// @file
/// @brief The motion a clip performs, measured from its authored tracks, and the motion-matching feature vector built
///        from it: the ONE measurement hky-tool's autotag and the plugin's motion matching share, so the database a
///        query searches is measured exactly as the tags were.
/// @note  Skyrim space: Z up, an actor faces +Y with +X to its right; lengths in game units, the clip's own.

namespace CB::engine::anim {

    struct MotionVec {
        double x = 0, y = 0, z = 0;
    };
    struct MotionQuat {
        double x = 0, y = 0, z = 0, w = 1;
    };

    /// @brief One clip's motion at every frame: the root (the actor's position and facing) and, when the clip carries
    ///        the humanoid chains, the feet and the pelvis in clip space.
    struct ClipMotion {
        double                                dt     = 1.0 / 30.0;   ///< seconds per frame
        int                                   frames = 0;
        std::vector<MotionVec>                rootPos;
        std::vector<MotionQuat>               rootRot;
        bool                                  hasFeet = false;        ///< both leg chains are present
        std::array<std::vector<MotionVec>, 2> foot;                   ///< [0] left, [1] right (the foot bones)
        bool                                  hasHip = false;
        std::vector<MotionVec>                hip;                    ///< the pelvis
    };

    /// @brief `def` sampled at every frame, with forward kinematics down the humanoid chains below the root.
    /// @return nullopt with `err` set when the clip has under two frames or no root translation and rotation.
    /// @note  The chains are named, not discovered: NPC Root -> COM -> Pelvis -> Thigh -> Calf -> Foot, as Skyrim's
    ///        humanoid skeleton parents them (verified against Skyrim.hky's skeleton.hkx). A clip without them still
    ///        measures its root; hasFeet / hasHip say what else it has.
    std::optional<ClipMotion> MeasureClip(const AnimationDef& def, std::string& err);

    /// @brief `v` in the frame of `q` (the inverse rotation applied).
    MotionVec ToLocal(const MotionQuat& q, const MotionVec& v);
    /// @brief `v` rotated by `q`.
    MotionVec Rotate(const MotionQuat& q, const MotionVec& v);

    /// @brief The motion-matching feature vector's layout (Holden's 27, at the predictor's horizons).
    /// @note  Every value is in the CURRENT frame's root space (x right, y forward, z up), game units:
    ///        trajectory positions and facing directions (x, y) at kHorizons; left and right foot positions and
    ///        velocities; the pelvis velocity.
    namespace features {
        inline constexpr std::array<double, 3> kHorizons{ 1.0 / 3.0, 2.0 / 3.0, 1.0 };   ///< seconds ahead
        inline constexpr int kTrajPos = 0;    ///< 6: (x, y) per horizon
        inline constexpr int kTrajDir = 6;    ///< 6: (x, y) per horizon, a unit vector
        inline constexpr int kFootPos = 12;   ///< 6: left (x, y, z), right (x, y, z)
        inline constexpr int kFootVel = 18;   ///< 6: left, right, game units/s
        inline constexpr int kHipVel  = 24;   ///< 3: game units/s
        inline constexpr int kDims    = 27;
    }

    /// @brief The clip's feature rows, one per frame (kDims floats each), appended to `rows`.
    /// @pre   `m.hasFeet && m.hasHip`.
    /// @note  A horizon past the clip's last frame holds at that frame: the database marks those frames unsearchable,
    ///        since their future is the clip ending, not the motion continuing.
    void AppendClipFeatures(const ClipMotion& m, std::vector<float>& rows);

}  // namespace CB::engine::anim
