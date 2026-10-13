// DiskUnitSource — the on-disk backing for IUnitSource.

#include <interface/UnitSource.h>

#include <disk/Disk.h>

#include <algorithm>

namespace CB::engine::common {

namespace fs = std::filesystem;

DiskUnitSource::DiskUnitSource(fs::path root) : m_root(std::move(root)) {}

std::vector<FileStamp> DiskUnitSource::stamps() const {
    std::vector<FileStamp> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(disk::LongPath(m_root), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        FileStamp s;
        s.rel   = it->path().lexically_relative(disk::LongPath(m_root)).generic_string();
        s.size  = it->file_size(fe);
        s.stamp = static_cast<std::uint64_t>(it->last_write_time(fe).time_since_epoch().count());
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const FileStamp& a, const FileStamp& b) { return a.rel < b.rel; });
    return out;
}

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
