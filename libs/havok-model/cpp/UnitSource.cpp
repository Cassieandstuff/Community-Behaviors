// DiskUnitSource — the on-disk backing for IUnitSource. A pure indirection over
// the filesystem access YamlBehaviorLoader used to do inline (ifstream +
// directory_iterator); its semantics are byte-for-byte what the loader did before
// the source abstraction, so the disk path is unchanged.

#include "havok/model/yaml/UnitSource.h"

#include <fstream>
#include <sstream>

namespace havok::model {

namespace fs = std::filesystem;

namespace {

// Binary slurp + UTF-8 BOM strip. Mirrors the old YamlBehaviorLoader::readFile:
// returns "" when the stream can't be opened OR the file is empty — the caller
// (read, below) distinguishes absence via fs::exists so it can map absent ->
// nullopt while keeping an empty-but-present file as "".
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    return s;
}

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
            if (fs::exists(cand)) return slurp(cand);
            if (!dir.has_parent_path() || dir.parent_path() == dir) break;
            dir = dir.parent_path();
        }
        return std::nullopt;
    }

    const fs::path p = m_root / rel;
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;   // absent -> nullopt (an empty-but-present file returns "")
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    return s;
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

} // namespace havok::model
