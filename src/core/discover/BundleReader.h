#pragma once

#include <codec/serialization/HkyArchive.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CB::engine::common { struct IUnitSource; }

namespace CB {

    // BundleReader — whole-bundle file access over a <Mod>.hky that is EITHER an
    // unpacked directory (author dev tree) OR a single packed .hky file (the shipped
    // form, e.g. the full-corpus Skyrim.hky master). The set-data / anim-data servers
    // address a bundle by whole-tree-relative paths (meshes/animationsetdata/…,
    // animationdata/…, animationnames/…, meshes/animationsetdatasinglefile.txt), so this
    // hides dir-vs-packed the way IUnitSource hides it per-unit for the Resolver.
    //
    // A packed bundle is read through its index (HkyArchive, MAX_PATH-immune). Lookups go
    // by path identity (codec::resource::PathKey), so `rel` may be spelled in any case; every
    // NAME handed back (subdirs, files, filesUnderOrig) keeps its original case, packed or
    // loose, and file CONTENT is verbatim. filesUnder() alone returns identity keys.
    class BundleReader {
    public:
        // Open a bundle. A directory opens directly; a regular .hky file is loaded as a
        // packed archive. Returns nullopt only if a packed .hky cannot be read.
        static std::optional<BundleReader> Open(const std::filesystem::path& bundle);

        // Read a whole-tree-relative file (e.g. "meshes/animationsetdatasinglefile.txt").
        // nullopt if absent; an empty-but-present file returns "".
        std::optional<std::string> read(const std::string& rel) const;

        // Immediate child directory NAMES under `dirPrefix`, ORIGINAL case (e.g. "meshes/animationsetdata"
        // -> {"DefaultMaleData", …}), packed or loose alike. Empty if the prefix is absent.
        std::vector<std::string> subdirs(const std::string& dirPrefix) const;

        // File BASENAMES, ORIGINAL case, directly under `dir` whose extension equals `extLower` (already
        // lowercased, incl. the dot, e.g. ".txt"; "" matches any; the extension test is path identity). Empty if absent.
        std::vector<std::string> files(const std::string& dir, const std::string& extLower) const;

        // Tree-relative paths (forward-slashed, lowercased) of every file RECURSIVELY under
        // `dirPrefix` whose extension equals `extLower` ("" = any). Unlike files(), this descends.
        // Feeds the per-actor animdata read (motion/movesets yamls scattered under the actor tree).
        std::vector<std::string> filesUnder(const std::string& dirPrefix, const std::string& extLower) const;

        // Like filesUnder, but ORIGINAL-CASE paths — use it whenever a NAME is derived from the path (a
        // clip or project name the engine matches exactly). Read-back goes by path identity, so it's safe.
        std::vector<std::string> filesUnderOrig(const std::string& dirPrefix, const std::string& extLower) const;

        /// @brief Tree-relative prefixes (forward-slashed, e.g. "meshes/actors/horse/characters/horse.hkx") of every
        ///        "<x>.hkx/" unit of `kind`, classified by its graph.yaml root (HkyArchive::KindOfGraphYaml); a
        ///        bonelist.yaml unit is a Skeleton.
        /// @note  A patch unit that leaves the root untouched has no graph.yaml and lists as a Behavior.
        std::vector<std::string> units(CB::engine::codec::HkyArchive::UnitKind kind) const;

        /// @brief A read-only IUnitSource rooted at a unit `prefix` (from units()): packed bundle -> ZipUnitSource,
        ///        unpacked -> DiskUnitSource, so a unit reads the same way regardless of form.
        std::shared_ptr<const CB::engine::common::IUnitSource> unitSource(const std::string& prefix) const;

    private:
        std::filesystem::path                     m_dir;   // set when unpacked (m_arc null)
        std::shared_ptr<CB::engine::codec::HkyArchive> m_arc;   // set when packed  (m_dir empty)
    };

}  // namespace CB
