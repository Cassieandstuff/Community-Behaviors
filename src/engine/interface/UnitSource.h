#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// IUnitSource — an abstract backing store for ONE unit (a single .hky layer: a base or a patch). The
// reflective reader reads every unit-relative file through it, so a unit lives on disk (DiskUnitSource)
// or in a packed .hky (ZipUnitSource) without the reader knowing which.
//
// A "unit-relative path" is the path of a file inside the unit, forward-slashed, e.g. "graph.yaml",
// "states/0000x0001.yaml", "states/0000x0001/transitions.yaml".

namespace CB::engine::common {

/// @brief One file of a unit as a cache key sees it: a value that changes whenever the file's content may have.
struct FileStamp {
    std::string   rel;     ///< unit-relative path, forward-slashed, case kept
    std::uint64_t size  = 0;
    std::uint64_t stamp = 0;   ///< packed: the file's CRC-32 from the zip index; on disk: its modification time
};

struct IUnitSource {
    virtual ~IUnitSource() = default;

    /// @brief Every file in the unit, stamped, in a stable order — read without opening any file's content.
    virtual std::vector<FileStamp> stamps() const = 0;

    // Read the file at unit-relative path `rel`. Returns std::nullopt if it does
    // not exist; an existing-but-empty file returns "" (an engaged empty string).
    // A UTF-8 BOM, if present, is stripped (ryml would otherwise fold it into the
    // first scalar).
    virtual std::optional<std::string> read(const std::string& rel) const = 0;

    // List unit-relative paths of *.yaml files directly under `subdir` (or the
    // whole subtree when `recursive`), e.g. listYaml("states", false) ->
    // {"states/1.yaml", ...}. Empty if the subdir is absent. Ordering matches the
    // backend's native enumeration (for DiskUnitSource, std::filesystem's
    // directory_iterator order — the loader groups nodes in first-seen order).
    virtual std::vector<std::string> listYaml(const std::string& subdir, bool recursive) const = 0;

    // True if `subdir` exists as a directory/prefix.
    virtual bool hasDir(const std::string& subdir) const = 0;
};

// One merge layer plus its load-order identity, so the merge can key node identity by SCOPE
// (which bundle's id-space a node belongs to) instead of by a bare id. `stem` is the owning
// bundle (lowercase; empty = anonymous, e.g. an offline/CLI source with no load-order context).
// When `stem` is empty for every layer (the offline path), scope resolution degenerates to bare-id
// grouping — identical to a merge with no load-order context.
struct LayerSource {
    std::shared_ptr<const IUnitSource> source;
    std::string                        stem;        // owning bundle (lowercase); empty = anonymous
    // The bundle's ORDERED master table (index 0 = base game, 1..k = declared masters, last = self),
    // built via CB::engine::codec::formid::BuildMasterTable. A node's FormId masterIndex resolves against THIS
    // -> the owning bundle stem. Empty for an anonymous/offline layer (no FormId
    // resolution -> the merge falls back to bare-id scope inference).
    std::vector<std::string>           masterTable;
};

// Default filesystem backing — reads from an on-disk unit directory `root`.
class DiskUnitSource : public IUnitSource {
public:
    explicit DiskUnitSource(std::filesystem::path root);

    std::vector<FileStamp> stamps() const override;
    std::optional<std::string> read(const std::string& rel) const override;
    std::vector<std::string> listYaml(const std::string& subdir, bool recursive) const override;
    bool hasDir(const std::string& subdir) const override;

private:
    std::filesystem::path m_root;
};

} // namespace CB::engine::common
