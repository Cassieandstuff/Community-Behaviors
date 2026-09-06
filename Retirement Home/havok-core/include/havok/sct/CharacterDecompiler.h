#pragma once
// CharacterDecompiler — the import direction for characters: a compiled
// character .hkx -> the character source tree (character.yaml + animations.txt +
// properties/*.yaml + _order.txt + foot_ik.yaml + mirror.yaml) that
// CharacterYamlLoader reads. Bone-weight / bone-pair maps are emitted in raw
// count+values form (skeleton-independent), so compile(decompile(x)) == x.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace havok::sct {

struct BoneNameTable;   // havok/sct/BoneNames.h — optional skeleton for bone-index -> name

struct DecompileResult {
    bool        ok = false;
    std::string error;
    std::string kind;   // "character" / "behavior" / "animation" when detected
};

// Deserialize `hkx`, detect the root dialect, and decompile it into `outDir`. `bones`
// (optional): when set, a behavior's bone-index fields decompile to bone NAMES; null =
// raw numbers (byte-identical to a no-skeleton decompile).
// Character is implemented; behavior/animation return a "not yet" error.
DecompileResult DecompileToDir(const std::vector<std::uint8_t>& hkx,
                               const std::filesystem::path& outDir,
                               const BoneNameTable* bones = nullptr);

} // namespace havok::sct
