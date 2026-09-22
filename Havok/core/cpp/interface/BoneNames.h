#pragma once
// BoneNames — the skeleton's bone list in index order, so a behavior graph's raw bone INDICES
// (hkbFootIkModifier bones, hkbBoneIndexArray, ROLE_BONE_INDEX bindings, …) can decompile to
// human bone NAMES and recompile back byte-exactly.
//
// A bone index is only meaningful against ONE skeleton's ordering, so the list is PINNED to the
// bundle (shipped as skeleton/<name>.txt, the same shape as animationnames/<char>.txt). Base
// (vanilla) + appendable overrides merge exactly like the roster fold: a bone already present keeps
// its index; a genuinely new bone (a skeleton extender's extra — XPMSSE) is appended. That mirrors
// how XPMSSE actually works (vanilla bones keep their slots, extras go on top), so vanilla indices
// are stable under the merge and index→name→index is identity against the merged table.

#include <string>
#include <unordered_map>
#include <vector>

namespace havok::sct {

    struct BoneNameTable {
        std::vector<std::string> names;   // index -> bone name (order = the skeleton's bone order)

        // name (case-insensitive) -> index. Rebuilt by Reindex(); call after mutating `names`.
        std::unordered_map<std::string, int> lut;

        void Reindex();

        bool empty() const { return names.empty(); }

        // Bone name at `index`, or nullptr if out of range.
        const std::string* NameOf(int index) const;

        // Index of `name` (case-insensitive), or -1 if absent.
        int IndexOf(const std::string& name) const;
    };

    // Parse a bone-list text: one bone name per line, blank lines and #/; comment lines skipped,
    // each line trimmed. Order is significant (it IS the bone index). Calls Reindex().
    BoneNameTable ParseBoneList(const std::string& text);

    // Serialize a table back to bone-list text (one name per line, "\r\n"), for shipping the base
    // skeleton/<name>.txt into Skyrim.hky. Round-trips with ParseBoneList.
    std::string EmitBoneList(const BoneNameTable& table);

    // Fold `overrideList` onto `base` the way animationnames folds a roster drop: keep base order
    // and indices, append any override bone NOT already present (case-insensitive) in original
    // order. Re-indexes `base`. Base bones therefore never shift, so a skeleton extender only ever
    // adds indices on top.
    void MergeBoneList(BoneNameTable& base, const BoneNameTable& overrideList);

}  // namespace havok::sct
