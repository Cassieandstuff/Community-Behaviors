#pragma once
// AnimationEmitter — AnimationDef -> a complete Skyrim SE animation packfile.
// Compresses via SplineCompressor, wraps the resulting
// hkaSplineCompressedAnimation in an hkaAnimationBinding + hkaAnimationContainer
// under an hkRootLevelContainer, and serializes to .hkx bytes. Port of HKBuild's
// AnimationHkxEmitter.cs.

#include "havok/anim/AnimationDef.h"
#include "havok/core/PackFileTypes.h"   // HKXHeader

#include <cstdint>
#include <vector>

namespace havok::anim {

// Returns the serialized .hkx byte buffer for `anim` (empty on failure — see the
// throwing internals; callers that need error text can wrap in try/catch).
std::vector<std::uint8_t> EmitAnimationHkx(const AnimationDef& anim, int fps = 30,
                                           const HKXHeader& header = HKXHeader::SkyrimSE());

} // namespace havok::anim
