#pragma once
#include "havok/core/HkTypes.h"

#include <cstdint>
#include <string>
#include <vector>

// Animation IMPORT — binary .hkx -> plain animation data, first-party, no .NET.
//
// Companion to SkeletonImport.h, and the second half of un-breaking the editor's
// import path: clip loading previously shelled out to SctBridge.dll to produce
// Havok packfile XML and re-parsed that.
//
// This deliberately stops at the DATA. The spline blob is handed over intact —
// havok-core does not decode it. The decoder (de Boor NURBS evaluation,
// THREECOMP40 quaternion unpacking, BITS8/16 dequantisation) already exists as
// first-party C++ in sct-pipeline and is input-agnostic; duplicating or moving
// it here would be churn for no gain. What changes is only where its inputs come
// from: deserialized fields instead of parsed XML text.

namespace havok::sct {

struct AnimationAnnotation {
    float       time = 0.f;
    std::string text;
};

struct AnimationAnnotationTrack {
    std::string                      trackName;
    std::vector<AnimationAnnotation> annotations;
};

struct AnimationData {
    // Which concrete class this came from. Interleaved animations populate
    // `transforms`; spline animations populate the block metadata and `data`.
    bool  isSpline = false;

    float duration            = 0.f;
    int   numTransformTracks  = 0;
    int   numFloatTracks      = 0;

    // ── hkaInterleavedUncompressedAnimation ──────────────────────────────────
    // numFrames * numTransformTracks entries, frame-major.
    std::vector<QSTransform> transforms;

    // ── hkaSplineCompressedAnimation ─────────────────────────────────────────
    int                        numFrames = 0;
    int                        numBlocks = 0;
    int                        maxFramesPerBlock = 0;
    int                        maskAndQuantizationSize = 0;
    std::vector<std::uint32_t> blockOffsets;      // byte offsets into `data`
    std::vector<std::uint32_t> transformOffsets;  // per-track, from block start
    std::vector<std::uint8_t>  data;              // opaque; decoded downstream

    // ── hkaAnimationBinding ──────────────────────────────────────────────────
    // Empty means identity (track i drives bone i) — the same fallback the XML
    // path used, since hkxcmd emits an empty list for identity mappings.
    std::vector<std::int16_t> transformTrackToBoneIndices;

    // ── Annotations ──────────────────────────────────────────────────────────
    // Carried through verbatim. SCT encodes face-morph keyframes in these.
    std::vector<AnimationAnnotationTrack> annotationTracks;
};

// Reads the FIRST animation in `bytes`, matching the old XML path's behaviour of
// taking the first supported hkobject. Skyrim ships one animation per file.
//
// Returns false and fills `err` on a malformed file or when the file contains no
// supported animation class.
bool LoadAnimationFromHkx(const std::uint8_t* data, std::size_t size,
                          AnimationData& out,
                          std::string* err = nullptr);

} // namespace havok::sct
