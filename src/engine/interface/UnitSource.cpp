// DiskUnitSource — the on-disk backing for IUnitSource.

#include <interface/UnitSource.h>

#include <disk/Disk.h>

namespace CB::engine::common {

namespace fs = std::filesystem;

namespace {

// The shared skeleton asset. Vanilla project layouts place it at an ANCESTOR of
// the behavior unit dir (project/character assets/skeleton.yaml, unit dirs nested
// below), so the disk backend resolves it by walking up the tree — byte-for-byte
// the pre-refactor findAndLoadSkeleton search (up to 12 levels, first hit wins).
// An in-memory backend simply packages skeleton.yaml unit-relative, so its plain
// read() suffices; the upward walk is a filesystem-only concern that lives here.
constexpr const char* kSkeletonRel = "character assets/skeleton.yaml";

} // namespace

DiskUnitSource::DiskUnitSource(fs::path root) : m_root(std::move(root)) {}

std::optional<std::string> DiskUnitSource::read(const std::string& rel) const {
    if (rel == kSkeletonRel) {
        fs::path dir = m_root;
        for (int depth = 0; depth < 12 && !dir.empty(); ++depth) {
            fs::path cand = dir / "character assets" / "skeleton.yaml";
            /// @note Found but unreadable reads as "" (not nullopt): the walk already proved the skeleton exists.
            if (fs::exists(cand)) return disk::ReadFile(cand, { .stripBom = true }).value_or(std::string());
            if (!dir.has_parent_path() || dir.parent_path() == dir) break;
            dir = dir.parent_path();
        }
        return std::nullopt;
    }

    /// @note Absent -> nullopt; an empty-but-present file -> "".
    return disk::ReadFile(m_root / rel, { .stripBom = true });
}

std::vector<std::string> DiskUnitSource::listYaml(const std::string& subdir, bool recursive) const {
    std::vector<std::string> out;
    const fs::path d = m_root / subdir;
    if (!fs::is_directory(d)) return out;
    auto take = [&](const fs::directory_entry& e) {
        if (e.is_regular_file() && e.path().extension() == ".yaml")
            out.push_back(fs::relative(e.path(), m_root).generic_string());
    };
    if (recursive) {
        for (auto& e : fs::recursive_directory_iterator(d)) take(e);
    } else {
        for (auto& e : fs::directory_iterator(d)) take(e);
    }
    return out;
}

bool DiskUnitSource::hasDir(const std::string& subdir) const {
    return fs::is_directory(m_root / subdir);
}

} // namespace CB::engine::common
