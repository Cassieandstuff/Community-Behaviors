#pragma once
// AnimationCompiler — AnimationDef -> validated .hkx, mirroring BehaviorCompiler.
// Thin wrapper over anim::EmitAnimationHkx that reuses sct::CompileResult.

#include "havok/anim/AnimationDef.h"
#include "havok/core/PackFileTypes.h"   // HKXHeader
#include "havok/sct/BehaviorCompiler.h" // CompileResult

#include <filesystem>

namespace havok::sct {

CompileResult CompileAnimation(const anim::AnimationDef& anim, int fps = 30,
                               const HKXHeader& header = HKXHeader::SkyrimSE());

CompileResult CompileAnimationToFile(const anim::AnimationDef&    anim,
                                     const std::filesystem::path& outPath,
                                     int                          fps = 30,
                                     const HKXHeader&             header = HKXHeader::SkyrimSE());

} // namespace havok::sct
