#pragma once
// AnimationDef — the authorable animation input model (port of HKBuild's
// Models/AnimationDef.cs). Plain glm-free POD structs: keyframed transform +
// float tracks that SplineCompressor resamples and compresses to the
// hkaSplineCompressedAnimation blob. Empty keyframe lists mean "no channel"
// (== the C# nullable list), handled by the sampler's defaults.

#include "havok/anim/AnimationData.h"   // animdata::MotionRecord

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace havok::anim {

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
    std::optional<havok::animdata::MotionRecord> motion;
};

} // namespace havok::anim
