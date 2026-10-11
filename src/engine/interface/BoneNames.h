#pragma once
// BoneNames — the skeleton's bone list in index order, so a behavior graph's raw bone INDICES
// (hkbFootIkModifier bones, hkbBoneIndexArray, ROLE_BONE_INDEX bindings, …) can decompile to
// human bone NAMES and recompile back byte-exactly.

#include <string>
#include <vector>

namespace CB::engine::common {

    struct BoneNameTable {
        std::vector<std::string> names;   // index -> bone name (order = the skeleton's bone order)

        bool empty() const { return names.empty(); }
    };

}  // namespace CB::engine::common
