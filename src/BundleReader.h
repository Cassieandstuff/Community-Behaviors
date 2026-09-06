#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace havok::model { class HkyArchive; struct IUnitSource; }

namespace CB {

    // BundleReader — whole-bundle file access over a <Mod>.hky that is EITHER an
    // unpacked directory (author dev tree) OR a single packed .hky file (the shipped
    // form, e.g. the full-corpus Skyrim.hky master). The set-data / anim-data servers
    // address a bundle by whole-tree-relative paths (meshes/animationsetdata/…,
    // animationdata/…, animationnames/…, meshes/animationsetdatasinglefile.txt), so this
    // hides dir-vs-packed the way IUnitSource hides it per-unit for the Resolver.
    //
    // A packed bundle is decompressed once in memory (HkyArchive, MAX_PATH-immune); its
    // path keys are lowercased, so every accessor here folds `rel` to lowercase/forward-
    // slash and returned dir/file names from a packed bundle are lowercase. File CONTENT
    // is returned verbatim (the singlefile-base bytes keep their original case). The
    // consumers already match case-insensitively, so a packed and an unpacked bundle
    // merge to the same result.
    class BundleReader {
    public:
        // Open a bundle. A directory opens directly; a regular .hky file is loaded as a
        // packed archive. Returns nullopt only if a packed .hky cannot be read.
        static std::optional<BundleReader> Open(const std::filesystem::path& bundle);

        bool packed() const { return static_cast<bool>(m_arc); }

        // Read a whole-tree-relative file (e.g. "meshes/animationsetdatasinglefile.txt").
        // nullopt if absent; an empty-but-present file returns "".
        std::optional<std::string> read(const std::string& rel) const;

        // Immediate child directory NAMES under `dirPrefix` (e.g. "meshes/animationsetdata"
        // -> {"defaultmaledata", …}). Empty if the prefix is absent.
        std::vector<std::string> subdirs(const std::string& dirPrefix) const;

        // File BASENAMES directly under `dir` whose extension equals `extLower` (already
        // lowercased, incl. the dot, e.g. ".txt"; "" matches any). Empty if absent.
        std::vector<std::string> files(const std::string& dir, const std::string& extLower) const;

        // Tree-relative paths (forward-slashed, lowercased) of every file RECURSIVELY under
        // `dirPrefix` whose extension equals `extLower` ("" = any). Unlike files(), this descends.
        // Feeds the per-actor animdata read (motion/movesets yamls scattered under the actor tree).
        std::vector<std::string> filesUnder(const std::string& dirPrefix, const std::string& extLower) const;

        // Like filesUnder, but ORIGINAL-CASE paths. Use ONLY for the animationdata clip/motion reads,
        // whose record NAME (derived from the filename) is case-sensitive to the engine's behaviour-clip
        // match — lowercasing it kills that clip's root motion. Read-back re-normalizes, so it's safe.
        std::vector<std::string> filesUnderOrig(const std::string& dirPrefix, const std::string& extLower) const;

        // Tree-relative prefixes of every CHARACTER unit (a dir carrying character.yaml),
        // forward-slashed, e.g. "meshes/actors/horse/characters/horse.hkx". Feeds set-data's
        // character -> actor-root map.
        std::vector<std::string> characterUnits() const;

        // Tree-relative prefixes of every BEHAVIOR unit (a dir carrying behavior.yaml),
        // forward-slashed, e.g. "meshes/actors/character/behaviors/community_behaviors/fnis.hkx".
        // Feeds the animationdata derive: a bundle's added clips live in its behavior units.
        std::vector<std::string> behaviorUnits() const;

        // A read-only IUnitSource rooted at a unit `prefix` (from behaviorUnits/characterUnits),
        // to feed YamlBehaviorLoader::LoadMerged — packed bundle -> ZipUnitSource, unpacked ->
        // DiskUnitSource, so the caller loads a unit's model the same way regardless of form.
        std::shared_ptr<const havok::model::IUnitSource> unitSource(const std::string& prefix) const;

    private:
        std::filesystem::path                     m_dir;   // set when unpacked (m_arc null)
        std::shared_ptr<havok::model::HkyArchive> m_arc;   // set when packed  (m_dir empty)
    };

}  // namespace CB
