#pragma once
// SkeletonYaml — the authoring / merge front end for the skeleton codec.
//
//   EmitSkeletonYaml     : SkeletonData -> combined single-file YAML (parent-by-NAME).
//   EmitSkeletonYamlTree : SkeletonData -> bonelist.yaml + bones/<name>.yaml (the .hky unit layout).
//   LoadSkeletonYaml     : a directory OR a single combined file -> SkeletonData (name→index via
//                          preserve-and-append: locked `index` prefix + topo-sorted appends).
//   *FromTexts           : in-memory twins (a packed .hky vends each unit as TEXT, never a disk path).
//   LoadSkeletonLayer / MergeBoneAdditions : bone-add layers (parent-by-name) appended onto a base.
//
// Pure SkeletonData ↔ YAML/text — no packfile, no typed classes. Ported from havok-core's sct/
// SkeletonYaml unchanged but for the namespace; it was already havok-core-free.

#include "havok/skeleton/SkeletonData.h"   // SkeletonData

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace havok::skeleton {

std::string EmitSkeletonYaml(const SkeletonData& data);

// Emit the per-bone TREE form (the authoring source layout): `bonelist.yaml` (index order) +
// `bones/<name>.yaml` (one file per bone). Returns false + `err` on write failure.
bool EmitSkeletonYamlTree(const SkeletonData& data, const std::filesystem::path& dir,
                          std::string* err = nullptr);

// Loads from `path` (a directory or a single combined YAML file). Returns false and fills `err` on
// unreadable input, an unknown bone/parent reference, or a parent cycle.
bool LoadSkeletonYaml(const std::filesystem::path& path, SkeletonData& out, std::string* err = nullptr);

// In-memory twin: a packed .hky vends each YAML unit as TEXT. `bonelistText` = the unit's
// bonelist.yaml (may be empty); `boneFiles` = {boneName, bones/<name>.yaml text} pairs (order-free).
bool LoadSkeletonYamlFromTexts(const std::string&                                       bonelistText,
                               const std::vector<std::pair<std::string, std::string>>&  boneFiles,
                               SkeletonData&                                            out,
                               std::string*                                             err = nullptr);

// ── bone-add layers (parent-by-name) ─────────────────────────────────────────────
struct SkeletonBoneAdd {
    std::string name;
    std::string parent;                // parent BY NAME ("" = root)
    QSTransform pose{};                // bind pose, bone-local
    bool        lockTranslation = false;
};

// Load a skeleton LAYER dir (bones/<name>.yaml; parent-by-name) as a list of additions. A layer
// `bonelist.yaml` with an ordered `index:` gives the source skeleton's native bone order (load-bearing:
// HKX-target animations bind tracks by INDEX). Without one, alphabetical. Returns false + `err` on bad input.
bool LoadSkeletonLayer(const std::filesystem::path& dir, std::vector<SkeletonBoneAdd>& out,
                       std::string* err = nullptr);

// In-memory twin of LoadSkeletonLayer — the layer vended as {boneName, text} pairs by a packed .hky.
bool LoadSkeletonLayerFromTexts(const std::vector<std::pair<std::string, std::string>>& boneFiles,
                                std::vector<SkeletonBoneAdd>&                            out,
                                const std::string&                                       bonelistText = "",
                                std::string*                                             err          = nullptr);

// Append `adds` onto a base anim skeleton: a NEW name is appended (parent by name, topo); an EXISTING
// name is left as the frozen base (extensions ADD, never rewrite vanilla). Feeds CompileSkeletonOverBase.
// Returns false + `err` on an unknown parent or an unresolvable cycle among the additions.
bool MergeBoneAdditions(SkeletonData& base, const std::vector<SkeletonBoneAdd>& adds,
                        std::string* err = nullptr);

} // namespace havok::skeleton
