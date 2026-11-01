#pragma once
#include <interface/AnimationDef.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace CB::engine::anim {

/// @brief One root-motion sample: translation (x, y, z) and rotation quaternion (x, y, z, w).
struct MotionPose {
    double t[3] = { 0, 0, 0 };
    double q[4] = { 0, 0, 0, 1 };
};

/// @brief A motion record evaluated EXACTLY as Skyrim's movement system samples it (AE 1.6.1170, RE'd):
///        `AdsfMotion_SampleTranslation_Lerp` / `AdsfMotion_SampleRotation_FastSlerp`.
/// @note Key times are IEEE-half quantized (the engine packs them through FloatToHalf); an implicit origin key
///       (t=0, zero translation / identity rotation) precedes the first key; time clamps to [0, lastKeyTime];
///       translation lerps, rotation uses the engine's approximate slerp (NiQuaternion_FastSlerp, no hemisphere flip).
///       A channel holding one identity key is dropped at load, exactly as `Adsf_ParseMotionBlock` does.
class EngineMotionCurve {
public:
    explicit EngineMotionCurve(const CB::engine::animdata::MotionRecord& m);
    MotionPose Sample(double time) const;

private:
    struct Key { double time; double v[4]; };
    std::vector<Key> m_translation;
    std::vector<Key> m_rotation;
};

/// @brief Fold a unit's `motion:` record onto its root track, as an animator's rig carries it: every frame the root
///        becomes M(t) · root(t), with M sampled by the engine evaluator. The `motion` record is cleared.
/// @pre `rootTrack` indexes the skeleton's root bone (`[Root]`, bone 0).
/// @return false (def untouched) when the unit has no motion or no frame timing.
bool FoldMotionOntoRoot(AnimationDef& def, std::size_t rootTrack, std::string* err = nullptr);

/// @brief Extract root motion HCT-style: Δ(t) = root(t) · root(0)⁻¹ projected to translation XYZ + yaw (the only
///        components the engine's motion carries), one key per frame after frame 0 (the origin is implicit), and strip
///        it from the root: root(t) ← M(t)⁻¹ · root(t).
/// @return the motion record (duration + dense keys), or nullopt when the root does not move (the root is unchanged).
std::optional<CB::engine::animdata::MotionRecord> ExtractRootMotion(AnimationDef& def, std::size_t rootTrack);

/// @brief How far a fold → extract round trip moves a unit, measured through the engine evaluator.
struct FoldCheck {
    double motionErr  = 0;   ///< max |M'(t) − M(t)| translation (units), frames + mid-frames
    double motionDeg  = 0;   ///< max rotation angle between M'(t) and M(t) (degrees)
    double visualErr  = 0;   ///< max |M'(t)·root''(t) − M(t)·root(t)| translation — what is ON SCREEN
    double visualDeg  = 0;
    double rootDrift  = 0;   ///< the source root's own travel (XYZ, relative to frame 0) — Bethesda's residual
    double rootYawDeg = 0;
    bool   extracted  = false;
    double worstTime  = 0;   ///< where motionErr peaks (seconds)
    double maxSpeed   = 0;   ///< peak motion speed (units/s, either curve) — the scale of half-time quantization noise
    double maxTurn    = 0;   ///< peak turn rate (deg/s)
    double keyTimeStep = 0;  ///< the engine's key-time resolution at the clip's end (one IEEE-half step, seconds)
};

/// @brief Fold the unit's motion onto `rootTrack`, extract it back, and measure against the source.
/// @return nullopt when the unit has no motion or no frame timing.
std::optional<FoldCheck> CheckFoldRoundTrip(const AnimationDef& def, std::size_t rootTrack);

/// @brief The fold gate's verdict for one unit: the source root is clean (Bethesda extracted all of its travel) and the
///        extracted motion matches the source within 0.01 + speed x one engine key-time step (IEEE-half key times: both
///        curves sit on that grid, so a smaller difference is the engine's own resolution, not a fold error).
bool FoldRoundTrips(const FoldCheck& c);

/// @brief How a unit carries its root motion.
enum class UnitMotionForm {
    None,       ///< no motion and a still root
    Folded,     ///< the root carries the motion (no `motion` key) — extracted at compile
    Explicit,   ///< a `motion:` record; the root is untouched (its travel stays visual)
    Visual,     ///< `motion: none` — the root moves, but only visually; nothing is extracted
};

/// @brief Settle a unit's motion form and rewrite its text: fold `motion` onto the root where the fold round-trips,
///        else write it as an explicit record; with no motion, mark a moving root rootMotionVisual. Any existing
///        motion is replaced; the text is re-emitted whole (EmitAnimationYaml).
/// @note Explicit stays until CB serves the stripped animation: the engine plays the SOURCE binary, whose root still
///       carries its visual travel, so extracting that travel into the actor too would double it on screen.
UnitMotionForm FinalizeUnitMotion(std::string& text, const std::optional<CB::engine::animdata::MotionRecord>& motion,
                                  std::string* err = nullptr);

/// @brief The unit's actor motion: its explicit record; nothing for `motion: none`; else extracted from the root,
///        which is stripped of it (so a compile of `def` afterwards serves the clean root).
std::optional<CB::engine::animdata::MotionRecord> ResolveRootMotion(AnimationDef& def, std::size_t rootTrack = 0);

} // namespace CB::engine::anim
