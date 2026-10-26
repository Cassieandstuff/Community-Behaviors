#pragma once
#include <codec/serialization/HkyArchive.h>
#include <decompile/BehaviorDecompile.h>
#include <codec/format/RigIndex.h>
#include <interface/SkeletonData.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vfs { class FileSystem; }

/// @file
/// @brief The resolved .hky load order as one document: every bundle under community_behaviors/plugins/ of a Data
///        folder (read through VFS), ordered as the game orders them, and every file and unit with the bundle that
///        wins it and every bundle that provides it. What an editor browses and pulls behaviors from.
/// @note  The same planning, layering and rig binding the plugin's Resolver uses (PlanLoadOrder, the Resolver's
///        layer rules, RigIndex), offline and CommonLib-free. Path identity folds (codec::resource::PathKey); every
///        returned path keeps its bundle's spelling.

namespace CB::engine::merge {

class LoadOrderDocument {
public:
    /// @brief One bundle of the load order.
    struct Bundle {
        std::string                         stem;       ///< PathKey'd file stem: the load-order handle
        std::string                         file;       ///< the .hky on disk (UTF-8)
        std::string                         provider;   ///< the VFS layer it comes from ("mod 'Foo'"), or "project"
        int                                 rank = 0;   ///< base-first position (higher wins)
        std::vector<std::string>            masters;    ///< declared in manifest.json
        bool                                dropped = false;   ///< a declared master is absent (it does not load)
        std::string                         error;      ///< why it could not be read; empty = read
        std::shared_ptr<codec::HkyArchive>  archive;
    };
    /// @brief One unit (a `<path>.hkx/` tree): its kind and its providers, base first (the last wins or merges last).
    struct Unit {
        std::string                    key;      ///< PathKey'd serve path
        std::string                    path;     ///< as the winning bundle spells it
        codec::HkyArchive::UnitKind    kind = codec::HkyArchive::UnitKind::Behavior;
        std::vector<std::string>       layers;   ///< bundle stems providing it, base first
    };

    /// @brief Read every .hky in `data`'s community_behaviors/plugins/ and community_behaviors/loadorder.txt, then
    ///        plan the order (masters first, loadorder.txt breaking ties) and bind each project's characters' rigs.
    /// @return false with `err` set when no bundle can be read
    bool Open(const vfs::FileSystem& data, const schema::SchemaRegistry& reg, std::string* err = nullptr);

    /// @brief Put the open project's bundle on top of the load order: it wins every file and merges last.
    /// @note  Replaces any earlier project; null removes it. The bundle's own manifest masters are not re-planned.
    void SetProject(std::shared_ptr<codec::HkyArchive> project, const std::string& stem);

    const std::vector<Bundle>&      Bundles() const { return m_bundles; }
    const std::vector<std::string>& Warnings() const { return m_warnings; }

    /// @brief Every file below `prefix` (a Data-relative folder; empty = all), as its winner spells it, sorted.
    std::vector<std::string> Files(std::string_view prefix = {}) const;
    /// @brief The bundle stems providing `path`, base first.
    std::vector<std::string> Providers(std::string_view path) const;
    /// @brief The winning bundle's text of `path` (an animation unit, a manifest, a config).
    std::optional<std::string> Text(std::string_view path) const;

    /// @brief Every unit below `prefix`, sorted by key.
    std::vector<Unit> Units(std::string_view prefix = {}) const;
    /// @brief The unit at `key`, or nullopt.
    std::optional<Unit> UnitAt(std::string_view key) const;

    /// @brief A behavior or character unit merged across its providers, its bone fields bound to the rig that plays it.
    /// @note  A project or skeleton unit is not merged: the load-order winner replaces it whole (Skeleton, Project*).
    bool MergedGraph(std::string_view key, decompile::ParsedHky& out, std::string* err = nullptr) const;
    /// @brief The rigs (skeleton unit keys) that bind `key`, sorted; several = a graph shared between characters.
    std::vector<std::string> Rigs(std::string_view key) const;
    /// @brief The animation bone names of the rig that binds `key` (a graph, a character, an animation), in served order.
    std::optional<std::vector<std::string>> RigBones(std::string_view key) const;
    /// @brief The skeleton at `key` (a skeleton unit), from its winning bundle.
    std::optional<skeleton::SkeletonData> Skeleton(std::string_view key, std::string* err = nullptr) const;
    /// @brief A character unit's strings (rig, behavior, animation roster) across its layers: each string from the
    ///        highest layer stating it, the roster every layer's (a patch's roster adds).
    std::optional<decompile::CharacterStrings> Character(std::string_view key) const;
    /// @brief The character units a project unit lists, from its winning bundle.
    std::vector<std::string> ProjectCharacters(std::string_view key) const;

private:
    void IndexBundles();
    std::shared_ptr<const common::IUnitSource> WinnerSource(const Unit& u) const;
    const Bundle* BundleNamed(std::string_view stem) const;

    const schema::SchemaRegistry*              m_reg = nullptr;
    std::vector<Bundle>                        m_bundles;    ///< base first; the project, when set, last
    std::vector<std::string>                   m_warnings;
    struct FileEntry {
        std::vector<std::size_t> bundles;   ///< indices into m_bundles, base first
        std::string              spelled;   ///< as the winning bundle spells it
    };
    std::map<std::string, FileEntry>           m_files;      ///< file key -> its providers
    std::map<std::string, Unit>                m_units;      ///< unit key -> unit
    skeleton::RigIndex                         m_rigIndex;   ///< projects' characters -> rigs
    std::map<std::string, std::vector<std::string>> m_rigBones;  ///< rig key -> animation bone names
    std::map<std::string, std::vector<std::string>> m_masterTable;   ///< stem -> ordered master table
};

}  // namespace CB::engine::merge
