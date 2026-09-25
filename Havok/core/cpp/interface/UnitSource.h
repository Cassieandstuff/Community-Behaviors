#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// IUnitSource — an abstract backing store for ONE behavior unit (a single .hky
// layer: a base or a delta). YamlBehaviorLoader reads every unit-relative file
// through this interface so a unit can live on disk (DiskUnitSource, the default)
// or, later, in memory (a BR zip/blob backend) without the loader knowing which.
//
// A "unit-relative path" is the path of a file inside the unit, forward-slashed,
// e.g. "behavior.yaml", "states/1.yaml", "data/additive.yaml",
// "character assets/skeleton.yaml".

namespace havok::model {

struct IUnitSource {
    virtual ~IUnitSource() = default;

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
// `rank` is the base-first load-order rank (lower = earlier; the master-DAG topo-sort assigns it).
// `ancestors` is the bundle's transitive master stems. When `stem`/`ancestors` are empty for every
// layer (the offline path), scope resolution degenerates to bare-id grouping — identical to a merge
// with no load-order context.
struct LayerSource {
    std::shared_ptr<const IUnitSource> source;
    std::string                        stem;        // owning bundle (lowercase); empty = anonymous
    int                                rank = 0;    // base-first load-order rank
    std::vector<std::string>           ancestors;   // transitive master stems (lowercase)
};

// Default filesystem backing — reads from an on-disk unit directory `root`.
class DiskUnitSource : public IUnitSource {
public:
    explicit DiskUnitSource(std::filesystem::path root);

    std::optional<std::string> read(const std::string& rel) const override;
    std::vector<std::string> listYaml(const std::string& subdir, bool recursive) const override;
    bool hasDir(const std::string& subdir) const override;

private:
    std::filesystem::path m_root;
};

} // namespace havok::model
