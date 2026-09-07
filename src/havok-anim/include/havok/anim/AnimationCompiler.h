#pragma once
// AnimationCompiler — AnimationDef -> validated .hkx, schema-native.
//
// The compile is SCHEMA-ONLY: AssembleAnimation builds an io::SchemaObject graph from the Havok/
// descriptors and the shared PackFileSerializer emits it. No typed hka* classes, no havok-core.
// The registry comes from havok::schema::SharedRegistry() (set once at startup); when it is
// unavailable the compile fails with a clear error instead of falling back to a typed path.

#include "havok/anim/AnimationDef.h"
#include "havok/core/PackFileTypes.h"   // HKXHeader (havok-framing)

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace havok::anim {

// Result of an animation compile. Same shape as havok::sct::CompileResult (kept local so havok-anim
// carries no havok-core dependency); callers read .ok / .error / .bytes unchanged.
struct AnimCompileResult {
    bool                      ok = false;
    std::string               error;   // populated when !ok
    std::vector<std::uint8_t> bytes;   // the packfile (when ok)
};

// Compile a loaded animation model straight to packfile bytes. Never throws — assembly/serializer
// exceptions are captured into AnimCompileResult::error. Requires havok::schema::SharedRegistry().
AnimCompileResult CompileAnimation(const AnimationDef& anim, int fps = 30,
                                   const HKXHeader& header = HKXHeader::SkyrimSE());

AnimCompileResult CompileAnimationToFile(const AnimationDef&          anim,
                                         const std::filesystem::path& outPath,
                                         int                          fps = 30,
                                         const HKXHeader&             header = HKXHeader::SkyrimSE());

} // namespace havok::anim
