#pragma once
// AnimationCompiler — AnimationDef -> validated .hkx, schema-native.
//
// The compile is SCHEMA-ONLY: AssembleAnimation builds an io::SchemaObject graph from the schema/
// descriptors and the shared PackFileSerializer emits it. The registry comes from
// CB::engine::schema::SharedRegistry() (set once at startup); when it is unavailable the compile
// fails with a clear error.

#include "interface/AnimationDef.h"
#include <codec/serialization/packfile/PackFileTypes.h>   // HKXHeader

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace CB::engine::anim {
using namespace CB::engine::codec;
using namespace CB::engine::common;

// Result of an animation compile; callers read .ok / .error / .bytes.
struct AnimCompileResult {
    bool                      ok = false;
    std::string               error;   // populated when !ok
    std::vector<std::uint8_t> bytes;   // the packfile (when ok)
};

// Compile a loaded animation model straight to packfile bytes. Never throws — assembly/serializer
// exceptions are captured into AnimCompileResult::error. Requires CB::engine::schema::SharedRegistry().
//
// `boneNames` (optional, index-parallel to the served skeleton): each track's authored bone reference
// is resolved to a bone index through the name<->index codec (BoneMembrane::resolveTrackRef), populating
// hkaAnimationBinding.transformTrackToBoneIndices — so a clip follows the served skeleton by name.
// null / empty => identity binding (the vanilla convention, byte-identical to an unbound clip).
AnimCompileResult CompileAnimation(const AnimationDef& anim, int fps = 30,
                                   const HKXHeader& header = HKXHeader::SkyrimSE(),
                                   const std::vector<std::string>* boneNames = nullptr);

AnimCompileResult CompileAnimationToFile(const AnimationDef&          anim,
                                         const std::filesystem::path& outPath,
                                         int                          fps = 30,
                                         const HKXHeader&             header = HKXHeader::SkyrimSE(),
                                         const std::vector<std::string>* boneNames = nullptr);

} // namespace CB::engine::anim
