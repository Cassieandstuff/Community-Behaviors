// DiskUnitSource — the on-disk backing for IUnitSource.

#include <interface/UnitSource.h>

#include <disk/Disk.h>

namespace CB::engine::common {

namespace fs = std::filesystem;

DiskUnitSource::DiskUnitSource(fs::path root) : m_root(std::move(root)) {}

std::optional<std::string> DiskUnitSource::read(const std::string& rel) const {
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
