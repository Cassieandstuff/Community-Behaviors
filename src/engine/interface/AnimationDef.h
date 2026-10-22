#pragma once
// AnimationDef — the authorable animation input model (port of HKBuild's
// Models/AnimationDef.cs). Plain glm-free POD structs: keyframed transform +
// float tracks that SplineCompressor resamples and compresses to the
// hkaSplineCompressedAnimation blob. Empty keyframe lists mean "no channel"
// (== the C# nullable list), handled by the sampler's defaults.

#include "interface/AnimationData.h"   // animdata::MotionRecord

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CB::engine::anim {

struct Vec3Keyframe {
    float time     = 0.f;
    float value[3] = {0.f, 0.f, 0.f};   // x, y, z
};

struct QuatKeyframe {
    float time     = 0.f;
    float value[4] = {0.f, 0.f, 0.f, 1.f};   // x, y, z, w
};

struct FloatKeyframe {
    float time  = 0.f;
    float value = 0.f;
};

struct AnimTrackDef {
    std::string               bone;
    std::vector<Vec3Keyframe> translation;   // empty == no channel
    std::vector<QuatKeyframe> rotation;
    std::vector<Vec3Keyframe> scale;
};

struct AnimFloatTrackDef {
    std::string                name;
    std::vector<FloatKeyframe> keyframes;
};

// hkaAnnotationTrackAnnotation — a timestamped text payload (e.g. an AMR
// root-motion or "MorphFace.<sys>|<morph>|<weight>" string) on a track.
struct AnimAnnotation {
    float       time = 0.f;
    std::string text;
};

// hkaAnnotationTrack — one annotation lane, named and carrying its entries.
// Havok stores one per transform track (most empty); the model preserves the
// authored/decompiled set verbatim so it round-trips byte-for-byte.
struct AnimAnnotationTrackDef {
    std::string                 trackName;
    std::vector<AnimAnnotation> annotations;
};

struct AnimCompressionParams {
    float rotationTolerance    = 0.001f;
    float translationTolerance = 0.001f;
    float scaleTolerance       = 0.001f;
    int   rotationDegree       = 3;   // authored default; the writers force linear
    int   translationDegree    = 1;
    int   scaleDegree          = 1;
    int   maxFramesPerBlock    = 256;
};

struct AnimationDef {
    std::string                    name;
    float                          duration = 0.f;
    // Native frame timing carried from the source so a recompile reproduces the EXACT frame count
    // (a 60fps source stays 60fps; a sparse 2-keyframe/6.7s pose stays 2 frames) instead of the
    // compiler resampling to round(duration×fps)+1. 0 == "not specified" → the compiler falls back to
    // the fps parameter (the authored-anim path, unchanged). See CompressAnimation.
    int                            numFrames     = 0;
    float                          frameDuration = 0.f;
    std::string                    skeleton;
    AnimCompressionParams          compression;
    std::vector<AnimTrackDef>      tracks;
    std::vector<AnimFloatTrackDef> floatTracks;
    std::vector<AnimAnnotationTrackDef> annotationTracks;

    // Root-motion record (the animationdata section-B form), OPTIONAL — absent when the clip has no
    // root motion (most placed cutscene actors). Authored last, under a top-level `motion:` key.
    // Carries duration + translation/rotation samples VERBATIM (byte-exact with the animationdata
    // .txt, same schema as EmitMotionSidecar/ParseMotionSidecar); `animIndex`/`animation` are NOT
    // authored here — the compiler binds them per-project at derive time from this animation's clip.
    std::optional<CB::engine::animdata::MotionRecord> motion;

    /// @brief `motion: none` — the root bone's travel is VISUAL only (Bethesda left it on the root and gave the clip no
    ///        actor motion: traps, chair exits). Never extracted. Absent `motion` (and not this) = the root carries the
    ///        actor motion, extracted at compile (RootMotion's ResolveRootMotion).
    bool rootMotionVisual = false;

    /// @brief `motionTarget:` — where the root motion goes, beside `motion` (where it comes from).
    /// @note  animationData (absent): the animation data file carries it and Havok moves the actor by it.
    ///        motionMatching: CB's motion matching measures it from the clip and the controller moves the actor, so the
    ///        animation data gets no record for the clip. Either way the compile strips the travel from the pose.
    enum class MotionTarget { AnimationData, MotionMatching };
    MotionTarget motionTarget = MotionTarget::AnimationData;

    /// @brief `preload: true` — the animation is loaded with its project, not when a clip first plays it: the animation
    ///        data file lists it in every project whose roster holds it, as vanilla lists an object's animations (a
    ///        door's open/close, a cave worm's four) beside keeping them rostered.
    /// @note  For animations that must play the moment they are asked for (an object's, as vanilla does it): a rostered
    ///        clip otherwise streams its file in when it activates. Unused by motion matching, whose generator keeps every
    ///        clip active instead.
    /// @warning Listing many in one project holds that project's actors back: with 252 listed in each humanoid project,
    ///          every humanoid (guards too) started invisible until it moved, and unlisting them fixed it (2026-10-21).
    bool preload = false;

    /// @brief `entry:` — where motion matching may cut into the clip. anywhere (absent): any frame. changes: only a frame
    ///        tagged Accelerate or Decelerate; a cut that landed there still plays on through what follows.
    /// @note  Why: in a long transition take (run-to-sprint and back), the steady stretches between the changes matched a
    ///        steady run as well as a run did, and 49 of 79 transition-take picks in one session landed on them.
    enum class Entry { Anywhere, Changes };
    Entry entry = Entry::Anywhere;

    /// @brief `tags:` — the motion-matching tags: each name's `[start, end)` frame ranges. Authored-YAML only (no Havok
    ///        field carries them, so a decompiled clip has none); written by the build's injection (hky-tool assemble).
    /// @note  nullopt = no `tags:` key; an empty map = the key with no tags. Names are compared byte-exact.
    std::optional<std::map<std::string, std::vector<std::pair<int, int>>>> tags;
};

} // namespace CB::engine::anim
