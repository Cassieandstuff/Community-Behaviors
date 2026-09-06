#pragma once
// SkeletonYaml — the authoring front end for the skeleton slice (Stage 1).
//
//   EmitSkeletonYaml : SkeletonData -> combined single-file YAML (parent-by-NAME).
//   LoadSkeletonYaml : a directory (skeleton.yaml + bones/<name>.yaml) OR a single
//                      combined file -> SkeletonData, resolving names -> indices via
//                      preserve-and-append (locked `index` prefix + topo-sorted appends).
//
// The two are a round-trip pair: EmitSkeletonYaml . LoadSkeletonYaml over a skeleton
// reproduces its bone name/parent/pose, which is the front-end's gate.
//
// Spelled-out pose field names; rotation quaternion is (x, y, z, w).

#include "havok/sct/SkeletonImport.h"   // SkeletonData

#include <filesystem>
#include <string>
#include <vector>

namespace havok::sct {

std::string EmitSkeletonYaml(const SkeletonData& data);

// Emit the per-bone TREE form (the authoring source layout): `bonelist.yaml` (index order) +
// `bones/<name>.yaml` (one file per bone: parent-by-name, lockTranslation, pose, optional physics).
// This is what a skeleton unit looks like inside a .hky bundle. Returns false + `err` on write failure.
bool EmitSkeletonYamlTree(const SkeletonData& data, const std::filesystem::path& dir,
                          std::string* err = nullptr);

// Loads from `path` (a directory or a single combined YAML file). Returns false and
// fills `err` on unreadable input, an unknown bone/parent reference, or a parent cycle.
bool LoadSkeletonYaml(const std::filesystem::path& path,
                      SkeletonData&                out,
                      std::string*                 err = nullptr);

// In-memory twin: a packed .hky vends each YAML unit as TEXT (never a disk path), so the runtime
// serve reads the base skeleton unit's files from the archive and hands them here.
// `bonelistText` = the unit's bonelist.yaml (may be empty); `boneFiles` = {boneName, bones/<name>.yaml
// text} pairs (order-free — sorted internally). Same assembly + validation as the directory loader.
bool LoadSkeletonYamlFromTexts(const std::string&                                       bonelistText,
                               const std::vector<std::pair<std::string, std::string>>&  boneFiles,
                               SkeletonData&                                            out,
                               std::string*                                             err = nullptr);

// ── Stage B/C: bone-add layers (parent-by-name) ─────────────────────────────────
// One authored bone addition — the atomic mergeable unit of a bone-add .hky plugin.
struct SkeletonBoneAdd {
    std::string name;
    std::string parent;                // parent BY NAME ("" = root)
    QSTransform pose{};                // bind pose, bone-local
    bool        lockTranslation = false;
};

// Load a skeleton LAYER dir (bones/<name>.yaml, each stem = the bone name; parent-by-name) as a
// list of additions. If the layer dir has a `bonelist.yaml` with an ordered `index:`, the additions
// come back in THAT order (the source skeleton's native bone order — load-bearing: HKX-target
// animations bind bone tracks by INDEX, so the appended bones must land at their source indices or
// every track maps to the wrong bone). Without a bonelist the order falls back to alphabetical.
// Returns false + `err` on bad input.
bool LoadSkeletonLayer(const std::filesystem::path& dir,
                       std::vector<SkeletonBoneAdd>& out,
                       std::string*                  err = nullptr);

// In-memory twin of LoadSkeletonLayer — the bone-add layer (skeleton/<actor>/bones/<name>.yaml)
// vended as {boneName, text} pairs by a packed .hky. `bonelistText` is the layer's bonelist.yaml
// contents (its ordered `index:` gives the source skeleton's native bone order); pass it to preserve
// the index-critical order. Empty `bonelistText` = alphabetical fallback.
bool LoadSkeletonLayerFromTexts(const std::vector<std::pair<std::string, std::string>>& boneFiles,
                                std::vector<SkeletonBoneAdd>&                            out,
                                const std::string&                                       bonelistText = "",
                                std::string*                                             err          = nullptr);

// Append `adds` onto a base anim skeleton (from the base skeleton.hkx = the frozen vanilla prefix):
// a NEW name is appended (parent resolved by name, topo — parent before child); an EXISTING name is
// left as the frozen base (extensions ADD, never rewrite vanilla). Feeds CompileSkeletonOverBase.
// Returns false + `err` on an unknown parent or an unresolvable cycle among the additions.
bool MergeBoneAdditions(SkeletonData&                       base,
                        const std::vector<SkeletonBoneAdd>& adds,
                        std::string*                        err = nullptr);

} // namespace havok::sct
