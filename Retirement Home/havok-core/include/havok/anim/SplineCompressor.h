#pragma once
// SplineCompressor — compresses an AnimationDef into the
// hkaSplineCompressedAnimation binary blob + its block metadata.
//
// Direct C++ port of HKBuild's SplineAnimCompressor.cs. Byte-exact discipline:
// float (not double) accumulation, C-style truncating quantization
// (int)(x + 0.5f), the same 1/sqrt(2) constant, and the same static-encoding
// quirks — so the output matches the C# encoder and round-trips through the
// first-party spline decoder. glm-free (plain scalar math).

#include "havok/anim/AnimationDef.h"

#include <cstdint>
#include <vector>

namespace havok::anim {

struct CompressedResult {
    int   numFrames               = 0;
    int   numBlocks               = 0;
    int   maxFramesPerBlock       = 0;
    float blockDuration           = 0.f;
    float blockInverseDuration    = 0.f;
    float frameDuration           = 0.f;
    int   maskAndQuantizationSize = 0;
    std::vector<std::uint32_t> blockOffsets;   // byte offset of each block in `data`
    std::vector<std::uint8_t>  data;
};

// Resample every track to a uniform `fps` grid, then spline-compress per block.
CompressedResult CompressAnimation(const AnimationDef& anim, int fps = 30);

} // namespace havok::anim
