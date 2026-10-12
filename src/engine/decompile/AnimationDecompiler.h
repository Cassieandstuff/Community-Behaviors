#pragma once
// AnimationDecompiler — the import leg: a compiled animation .hkx -> animation.yaml that
// AnimationYamlLoader reads back. Schema-native: the packfile is deserialized through the generic
// io::SchemaObject path (MakeSchemaFactory over the shared registry), so a real game animation's
// second variant (hkMemoryResourceContainer) round-trips. Only hkaSplineCompressedAnimation is
// decompiled (see the .cpp for round-trip fidelity).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace CB::engine::anim {

struct AnimDecompileResult {
    bool        ok = false;
    std::string error;
};

// Deserialize `hkx` (raw packfile bytes) via the schema stack, decode the first spline animation, and
// emit `dir`/animation.yaml. `boneNames` (optional, index-parallel to the served skeleton): when set,
// per-track bone references decompile to bone NAMES via the cross membrane (Phase 2); null = the
// track<N> placeholder names. Requires CB::engine::schema::SharedRegistry().
AnimDecompileResult DecompileAnimation(const std::vector<std::uint8_t>& hkx,
                                       const std::filesystem::path&      dir,
                                       const std::vector<std::string>*   boneNames = nullptr);

} // namespace CB::engine::anim
