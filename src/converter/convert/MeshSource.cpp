#include "MeshSource.h"

#include <codec/resource/PathKey.h>
#include <disk/Disk.h>

#include <algorithm>

namespace CB::converter {

namespace fs = std::filesystem;
using CB::engine::codec::resource::PathKey;

std::optional<MeshSource> MeshSource::OpenRegenFolder(const fs::path& folder, std::string& err) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) { err = "regen source not found: " + folder.string(); return std::nullopt; }
    MeshSource src;
    if (PathKey(folder.filename().string()) == "meshes") {
        src.AddLoose(folder.parent_path());
        return src;
    }
    if (fs::is_directory(folder / "meshes", ec)) src.AddLoose(folder);
    std::vector<fs::path> base, rest;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || PathKey(it->path().extension().string()) != ".bsa") continue;
        (PathKey(it->path().filename().string()).rfind("skyrim - ", 0) == 0 ? base : rest).push_back(it->path());
    }
    const auto byName = [](const fs::path& a, const fs::path& b) { return PathKey(a.filename().string()) < PathKey(b.filename().string()); };
    std::sort(base.begin(), base.end(), byName);
    std::sort(rest.begin(), rest.end(), byName);
    for (const auto* group : { &base, &rest })
        for (const auto& p : *group)
            if (!src.AddArchive(p, err)) return std::nullopt;
    if (src.m_byKey.empty()) { err = folder.string() + " holds no meshes\\ tree and no archive with meshes"; return std::nullopt; }
    return src;
}

void MeshSource::AddLoose(const fs::path& dataRoot) {
    std::error_code ec;
    const fs::path meshes = dataRoot / "meshes";
    m_looseRoots.push_back(meshes);
    for (fs::recursive_directory_iterator it(meshes, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe) || fe) continue;
        const std::string rel = "meshes/" + it->path().lexically_relative(meshes).generic_string();
        auto& o = m_byKey[PathKey(rel)];
        o = Origin{ rel, it->path(), 0, nullptr };
    }
    m_filesStale = true;
}

bool MeshSource::AddArchive(const fs::path& path, std::string& err) {
    std::string aerr;
    auto arc = vfs::bsa::Archive::Open(path, &aerr);
    if (!arc) { err = "archive unreadable: " + path.string() + " — " + aerr; return false; }
    m_archives.push_back(std::make_unique<vfs::bsa::Archive>(std::move(*arc)));
    m_archivePaths.push_back(path);
    const std::size_t ai = m_archives.size() - 1;
    for (const auto& e : m_archives[ai]->Entries()) {
        if (PathKey(e.path).rfind("meshes/", 0) != 0) continue;
        std::string rel = e.path;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const std::string key = PathKey(rel);
        const auto it = m_byKey.find(key);
        if (it != m_byKey.end()) {
            if (!it->second.loose.empty()) continue;   // a loose file beats every archive
            ++m_overridden;
        }
        m_byKey[key] = Origin{ std::move(rel), {}, ai, &e };
    }
    m_filesStale = true;
    return true;
}

const std::vector<std::string>& MeshSource::Files() const {
    if (m_filesStale) {
        m_files.clear();
        m_files.reserve(m_byKey.size());
        for (const auto& [key, o] : m_byKey) m_files.push_back(o.rel);
        m_filesStale = false;
    }
    return m_files;
}

std::optional<std::vector<std::uint8_t>> MeshSource::Read(const std::string& rel, std::string* err) const {
    const auto it = m_byKey.find(PathKey(rel));
    if (it == m_byKey.end()) { if (err) *err = "not in the source: " + rel; return std::nullopt; }
    const Origin& o = it->second;
    if (!o.loose.empty()) {
        auto bytes = CB::engine::disk::ReadBytes(o.loose);
        if (!bytes && err) *err = "cannot open for reading: " + o.loose.string();
        return bytes;
    }
    std::vector<std::uint8_t> out;
    if (!m_archives[o.archive]->Extract(*o.entry, out, err)) return std::nullopt;
    return out;
}

std::optional<std::string> MeshSource::ReadText(const std::string& rel) const {
    auto bytes = Read(rel);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

bool MeshSource::Has(const std::string& rel) const { return m_byKey.count(PathKey(rel)) != 0; }

std::string MeshSource::Describe() const {
    std::string s = std::to_string(m_byKey.size()) + " files from ";
    for (std::size_t i = 0; i < m_looseRoots.size(); ++i) s += (i ? ", " : "") + ("loose " + m_looseRoots[i].string());
    if (!m_looseRoots.empty() && !m_archivePaths.empty()) s += " + ";
    if (!m_archivePaths.empty()) s += std::to_string(m_archivePaths.size()) + " archive(s)";
    if (m_overridden) s += " (" + std::to_string(m_overridden) + " archive path(s) overridden by a later archive)";
    return s;
}

}  // namespace CB::converter
