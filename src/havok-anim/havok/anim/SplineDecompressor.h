#pragma once
// SplineDecompressor — decodes an hkaSplineCompressedAnimation blob into
// per-frame per-track local transforms. glm-free (havok-core carries no glm);
// faithful port of the verified decoder in sct-pipeline (de Boor NURBS for
// translation/scale, THREECOMP40/48 + UNCOMPRESSED rotation, BITS8/16). This is
// the decode half of the codec, colocated with the encoder so havok-core owns
// the full spline round-trip; the pose-evaluation layer stays in sct-pipeline.

#include <cstdint>
#include <string>
#include <vector>

namespace havok::anim {

struct DecodedPose {
    float t[3] = {0.f, 0.f, 0.f};        // local translation
    float q[4] = {0.f, 0.f, 0.f, 1.f};   // local rotation, x,y,z,w
    float s[3] = {1.f, 1.f, 1.f};        // local scale (identity == 1,1,1)
};

// Decode into `out` sized numFrames*numTracks, index = frame*numTracks + track.
// Uses the sequential reader (the validated path — transformOffsets are NOT
// consulted). Returns false on a bounds/format mismatch. `warn`, if non-null,
// receives a note when a track uses a rotation quantization that is strided but
// not reconstructed (those tracks get identity rotations).
//
// `floatOut`, if non-null, is filled with the per-frame float-track values,
// sized numFrames*numFloatTracks, index = frame*numFloatTracks + floatTrack.
// Static floats (mask 0x03 — the only form seen across a large real corpus) are
// exact; dynamic float channels use the encoder's 1-component BITS16 spline.
bool DecodeSpline(const std::uint8_t* data, std::size_t dataLen,
                  int numFrames, int numBlocks, int maxFramesPerBlock,
                  int maskAndQuantizationSize,
                  const std::uint32_t* blockOffsets, int numBlockOffsets,
                  int numTracks, int numFloatTracks,
                  std::vector<DecodedPose>& out, std::string* warn = nullptr,
                  std::vector<float>* floatOut = nullptr);

} // namespace havok::anim
