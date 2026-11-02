#pragma once
// AnimationDecompiler — the import leg: a compiled animation .hkx -> animation.yaml that
// AnimationYamlLoader reads back. Schema-native: the packfile is deserialized through the generic
// io::SchemaObject path (MakeSchemaFactory over the shared registry), so a real game animation's
// second variant (hkMemoryResourceContainer) round-trips. hkaSplineCompressedAnimation and
// hkaInterleavedUncompressedAnimation are decoded (see the .cpp for round-trip fidelity).

#include "codec/spline/SplineDecompressor.h"
#include "interface/AnimationDef.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace CB::engine::anim {

struct AnimDecompileResult {
    bool        ok = false;
    std::string error;
};

/// @brief An animation .hkx decoded into memory: the sampled pose stream plus everything the YAML emit carries.
/// @invariant poses.size() == numFrames * numTracks, frame-major (index = frame * numTracks + track);
///            floats.size() == numFrames * numFloatTracks, frame-major.
struct DecodedAnimation {
    std::string animClass;       ///< hkaSplineCompressedAnimation or hkaInterleavedUncompressedAnimation
    float       duration      = 0.f;
    int         numFrames     = 0;
    float       frameDuration = 0.f;   ///< spline: stored; interleaved: duration / (numFrames - 1)
    int         numTracks      = 0;
    int         numFloatTracks = 0;
    std::vector<codec::spline::DecodedPose> poses;
    std::vector<float>                      floats;
    /// @brief hkaAnimationBinding.transformTrackToBoneIndices; EMPTY is the identity binding (track t -> bone t).
    std::vector<std::int16_t>           trackToBone;
    std::string                         skeleton;   ///< the binding's originalSkeletonName ("" without a binding)
    std::vector<AnimAnnotationTrackDef> annotationTracks;   ///< verbatim, in file order
    /// @brief Non-empty when a track uses a rotation quantization that is strided but not reconstructed
    ///        (that track decodes to identity rotations).
    std::string warning;
};

/// @brief Deserialize `hkx` via the schema stack and decode its first animation into memory.
/// @pre   CB::engine::schema::SharedRegistry() is available (SetSharedSchemaDir or SCT_HAVOK_SCHEMA_DIR).
/// @return false (+ `err`) for a malformed packfile, no animation container, or an unsupported class.
bool DecodeAnimation(const std::uint8_t* hkx, std::size_t size, DecodedAnimation& out, std::string& err);

/// @brief Decode the first animation of `hkx` (raw packfile bytes) and write `dir`/animation.yaml (EmitAnimationYaml).
/// @param boneNames index-parallel to the served skeleton: tracks decompile to bone NAMES; null = track<N> placeholders.
/// @param name the unit's `name` (the clip's file stem); empty writes none.
/// @pre   CB::engine::schema::SharedRegistry() is loaded.
AnimDecompileResult DecompileAnimation(const std::vector<std::uint8_t>& hkx,
                                       const std::filesystem::path&      dir,
                                       const std::vector<std::string>*   boneNames = nullptr,
                                       const std::string&                name      = {});

} // namespace CB::engine::anim
