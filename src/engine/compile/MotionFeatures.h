#pragma once

#include "interface/AnimationDef.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief The motion a clip performs, measured from its authored tracks, and the motion-matching feature vector built
///        from it: the ONE measurement hky-tool's motionCurves generator and the plugin's motion matching share, so the
///        database a query searches is measured exactly as its curves were.
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
        std::array<std::vector<MotionVec>, 2> thigh;                  ///< [0] left, [1] right: where each leg leaves the pelvis
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

    /// @brief The motion-matching feature vector's layout: trajectory samples ahead and back, then the pose.
    /// @note  Every value is in the CURRENT frame's root space (x right, y forward, z up), game units. Each sample holds
    ///        the root's position (x, y), facing (x, y, a unit vector), velocity (x, y, game units/s) and height (z).
    ///        The pose: left and right foot positions and velocities, the pelvis velocity, the left foot from the right,
    ///        and the pelvis's facing. Everything from kFootPos on is the pose.
    /// @note  One row holds every part any database matches on; each database weighs the parts (PoseSearch's
    ///        DefaultWeights / ApplyWeights), so one measurement serves all and a weight of 0 leaves a part out.
    /// @note  The samples cover GASP's schemas: 0.2, 0.35, 0.5, 0.7 and 1 s ahead and 0.05 and 0.2 s back (PSS_Default,
    ///        PSS_Stop, PSS_Jump). 1/3 and 2/3 stand for 0.35 and 0.7, the horizons CB matched on first.
    /// @note  Why the past reaches 1 s: with only the future, a steady stretch inside a gait-change take (run-to-sprint)
    ///        matched a steady run as well as a run did, and won about a quarter of the picks; where the motion came from
    ///        tells them apart (Unreal's pose history plays the same part).
    namespace features {
        inline constexpr std::array<double, 5> kAhead{ 0.2, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0 };    ///< seconds ahead
        inline constexpr std::array<double, 5> kBack{ 0.05, 0.2, 1.0 / 3.0, 2.0 / 3.0, 1.0 };   ///< seconds back
        inline constexpr int kSamples   = 10;   ///< kAhead's, then kBack's
        inline constexpr int kPerSample = 7;
        inline constexpr int kSamplePos = 0, kSampleFacing = 2, kSampleVelocity = 4, kSampleHeight = 6;   ///< within a sample
        inline constexpr int kFootPos      = kSamples * kPerSample;   ///< 6: left (x, y, z), right (x, y, z)
        inline constexpr int kFootVel      = kFootPos + 6;            ///< 6: left, right, game units/s
        inline constexpr int kHipVel       = kFootVel + 6;            ///< 3: game units/s
        inline constexpr int kFootSpacing  = kHipVel + 3;             ///< 3: the left foot from the right
        inline constexpr int kPelvisFacing = kFootSpacing + 3;        ///< 2: the hips' forward, a unit vector in the ground plane
        inline constexpr int kDims         = kPelvisFacing + 2;
        /// @return the first dimension of sample `s`
        constexpr int SampleAt(int s) { return s * kPerSample; }
        /// @return the time of sample `s`, seconds from now (negative: back)
        constexpr double SampleTime(int s) { return s < static_cast<int>(kAhead.size()) ? kAhead[s] : -kBack[s - kAhead.size()]; }
    }

    /// @brief How a clip's last frame meets its first, read from its feature rows' pose (the feet and pelvis in root space).
    struct LoopFit {
        double footPos = 0;   ///< the largest foot position difference, game units
        double footVel = 0;   ///< the largest foot velocity difference, game units/s
        double hipVel  = 0;   ///< the largest pelvis velocity difference, game units/s
        double step    = 0;   ///< the feet's largest one-frame travel at the ends, game units
        int    period  = 0;   ///< frames per cycle: frames - 1 (the last frame repeats the first), frames (it runs on into it), 0 = no loop
    };
    /// @brief How well a clip's ends meet, from its feature rows' pose.
    /// @note  From the motion rather than a flag: Unreal keeps a sequence's bLoop on the asset, which no export carries.
    /// @note  The end foot velocities are one-sided differences from either side of the seam, so they disagree even on a
    ///        clean loop; the pelvis velocity tells a start or stop from a cycle.
    LoopFit FitLoop(const float* rows, int frames, double dt);
    /// @brief The clip's loop period: FitLoop's, refused for a clip standing at both ends whose root turns or travels
    ///        between (a turn or step in place starts and ends in the same stance).
    int LoopPeriod(const ClipMotion& m, const float* rows);

    /// @brief The clip's feature rows, one per frame (kDims floats each), appended to `rows`.
    /// @note  A clip that loops (FitLoop) samples its trajectory across the seam, its root carried on by one cycle's
    ///        motion, so a frame near its end sees the next cycle ahead rather than the clip stopping.
    /// @pre   `m.hasFeet && m.hasHip`.
    /// @note  A sample past the clip's last frame holds at that frame: the database marks those frames unsearchable,
    ///        since their future is the clip ending, not the motion continuing. A sample back before the first frame
    ///        holds at the first: a start clip's past is where it starts (standing), and every frame stays searchable.
    /// @return the clip's loop period (LoopPeriod), 0 when it does not loop
    int AppendClipFeatures(const ClipMotion& m, std::vector<float>& rows);

    /// @brief How far each measured curve's keys may stray from its per-frame measurement (the curve's own units).
    struct CurveTolerances {
        double speed         = 1.0;   ///< units/s
        double moveDirection = 1.0;   ///< degrees
        double turnRate      = 2.0;   ///< deg/s
        double acceleration  = 5.0;   ///< units/s²
    };

    /// @brief The clip's measured curves, simplified within `tol`: Speed (horizontal root speed, units/s),
    ///        MoveDirection (travel relative to facing, degrees, -180..180, + = right), TurnRate (facing turn rate,
    ///        deg/s, + = right), Acceleration (d Speed / dt, units/s²) and, with the leg chains, LeftFootLock and
    ///        RightFootLock (planted 1, else 0).
    /// @note  MoveDirection holds its last moving value while Speed is under 15 units/s (standing has no direction),
    ///        and a key before a ±180 wrap is CONSTANT, so no sample between two frames sweeps through 0.
    /// @note  Names are compared byte-exact by every reader; the generator and motion matching share these.
    std::vector<AnimCurveDef> MeasureMotionCurves(const ClipMotion& m, const CurveTolerances& tol);

    /// @brief A skeleton's bones, parents before children or not: what a schema's features measure through.
    /// @note  An animation names only the bones it moves; the rest hold their reference pose, and parents come from here.
    struct Rig {
        struct Bone {
            std::string name;          ///< byte-exact
            int         parent = -1;
            MotionVec   translation;   ///< the reference pose, parent-relative
            MotionQuat  rotation;
        };
        std::vector<Bone> bones;
        /// @return the bone named `name` exactly, -1 when none
        int IndexOf(std::string_view name) const;
    };

    /// @brief CBPoseSearch*'s feature classes, as plain data (the schema classes' fields, names byte-exact).
    enum class FeatureKind { Trajectory, Position, Velocity, Heading, Curve };
    struct TrajectorySampleSpec {
        float time = 0;                                           ///< seconds; negative = back
        float position = 0, facing = 0, velocity = 0, height = 0;  ///< each part's weight
    };
    struct FeatureSpec {
        FeatureKind                       kind   = FeatureKind::Trajectory;
        float                             weight = 1;
        std::string                       bone, origin, curve;   ///< as the kind uses them; origin empty = the root bone
        int                               axis = 1;               ///< Heading: 0 = x, 1 = y, 2 = z
        std::vector<TrajectorySampleSpec> samples;               ///< Trajectory
    };
    struct SchemaSpec {
        std::string              name;
        std::string              rootBone;   ///< empty = the rig's first bone
        std::vector<FeatureSpec> features;
    };

    /// @brief Floats a feature adds to a row: a trajectory 7 per sample (position x y, facing x y, velocity x y, height),
    ///        a position or velocity 3, a heading 2, a curve 1.
    int FeatureDims(const FeatureSpec& f);
    /// @brief A schema's row width: its features' dims, in order.
    int SchemaDims(const SchemaSpec& s);
    /// @brief Each dim's weight: the feature's weight, times the sample part's for a trajectory.
    std::vector<float> SchemaWeights(const SchemaSpec& s);

    /// @brief `def` measured against `schema` through `rig`: one row (SchemaDims floats) per frame.
    /// @param loopFrames the clip's loop period (LoopPeriod), 0 = no loop: a loop's trajectory samples wrap into its next
    ///        cycle, anything else's hold at its ends
    /// @return nullopt with `err` set when the clip has under two frames, or the rig lacks a bone a feature or the root names.
    /// @note  Every value is in the current frame's root space (x right, y forward, z up), game units.
    std::optional<std::vector<float>> MeasureSchema(const AnimationDef& def, const Rig& rig, const SchemaSpec& schema, int loopFrames,
                                                    std::string& err);

}  // namespace CB::engine::anim
