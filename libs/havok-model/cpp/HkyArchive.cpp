#include "havok/model/yaml/HkyArchive.h"

#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace havok::model {

namespace {

std::string norm(std::string s) {   // lowercase + forward-slash — matches loader reads + serve keys
    for (char& c : s) {
        if (c == '\\') c = '/';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string slashOnly(std::string s) {   // '\'->'/' but PRESERVE case (the original path for enumeration)
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

bool endsWith(const std::string& s, const char* suf) {
    const std::size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// IUnitSource over a subtree of an HkyArchive rooted at `prefix`. Holds the archive
// alive (shared_ptr) so a source can outlive the local that produced it.
class ZipUnitSource final : public IUnitSource {
public:
    ZipUnitSource(std::shared_ptr<const HkyArchive> arc, std::string prefix)
        : m_arc(std::move(arc)), m_root(std::move(prefix) + "/") {}

    std::optional<std::string> read(const std::string& rel) const override {
        return m_arc->file(m_root + norm(rel));
    }

    std::vector<std::string> listYaml(const std::string& subdir, bool recursive) const override {
        const std::string base = m_root + norm(subdir) + "/";
        std::vector<std::string> out;
        for (const auto& full : m_arc->filesUnder(base)) {
            if (!endsWith(full, ".yaml")) continue;
            const std::string afterBase = full.substr(base.size());
            if (!recursive && afterBase.find('/') != std::string::npos) continue;
            out.push_back(full.substr(m_root.size()));   // unit-relative (e.g. "states/1.yaml")
        }
        return out;   // m_files is ordered -> deterministic
    }

    bool hasDir(const std::string& subdir) const override {
        return !m_arc->filesUnder(m_root + norm(subdir) + "/").empty();
    }

private:
    std::shared_ptr<const HkyArchive> m_arc;
    std::string                       m_root;   // "<prefix>/"
};

}  // namespace

std::shared_ptr<HkyArchive> HkyArchive::LoadFromFile(const std::string& hkyPath, std::string& err) {
    mz_zip_archive z;
    std::memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, hkyPath.c_str(), 0)) { err = "cannot open .hky: " + hkyPath; return nullptr; }

    auto arc = std::shared_ptr<HkyArchive>(new HkyArchive());
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; ++i) {
        if (mz_zip_reader_is_file_a_directory(&z, i)) continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) { mz_zip_reader_end(&z); err = "corrupt .hky (stat)"; return nullptr; }
        std::size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&z, i, &sz, 0);
        if (!p) { mz_zip_reader_end(&z); err = std::string("extract failed: ") + st.m_filename; return nullptr; }
        arc->m_files.emplace(norm(st.m_filename),
                             Entry{ slashOnly(st.m_filename), std::string(static_cast<const char*>(p), sz) });
        mz_free(p);
    }
    mz_zip_reader_end(&z);

    // Index units: EVERY "<prefix>.hkx/..." subtree is a unit. The KIND comes from a base-marker
    // yaml when present (character/project.yaml; behavior is the default). A mod DELTA carries no
    // marker yaml — just changed-node subfolders (states/, clips/, data/) — yet is a real layer,
    // so it indexes as a Behavior unit. Keying only on behavior.yaml (as before) made packed mod
    // bundles index ZERO units, silently dropping every delta (graphs compiled base-only = vanilla).
    // m_files is sorted, so all files under one root are consecutive — dedup by tracking the last.
    std::string lastRoot;
    for (const auto& [k, _] : arc->m_files) {
        const auto pos = k.find(".hkx/");
        if (pos == std::string::npos) continue;
        std::string root = k.substr(0, pos + 4);          // include ".hkx"
        if (root == lastRoot) continue;
        lastRoot = root;
        UnitKind kind = UnitKind::Behavior;
        if      (arc->m_files.count(root + "/bonelist.yaml"))  kind = UnitKind::Skeleton;   // skeleton tree, NOT a graph
        else if (arc->m_files.count(root + "/character.yaml")) kind = UnitKind::Character;
        else if (arc->m_files.count(root + "/project.yaml"))   kind = UnitKind::Project;
        else if (arc->m_files.count(root + "/bonelist.yaml"))  kind = UnitKind::Skeleton;   // skeleton*.hkx unit
        arc->m_units.push_back({ std::move(root), kind });
    }
    if (arc->m_files.empty()) { err = ".hky is empty: " + hkyPath; return nullptr; }
    return arc;
}

bool HkyArchive::PackDirectory(const std::string& dir, const std::string& outHkyPath, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { err = "not a directory: " + dir; return false; }

    fs::path rootPath = fs::absolute(fs::path(dir), ec);
    if (ec) { err = "cannot resolve: " + dir; return false; }
#ifdef _WIN32
    // Long-path (\\?\) prefix so a DEEP unit tree — which overruns MAX_PATH, the very reason
    // the runtime HkyArchive never extracts to disk — can still be walked and read here. The
    // prefix rides on every yielded path, so both the iterator and the per-file ifstream open
    // the long path. It cancels out in lexically_relative(rootPath) (both sides carry it).
    {
        const std::wstring pfx = L"\\\\?\\";
        std::wstring       w   = rootPath.native();
        if (w.compare(0, pfx.size(), pfx) != 0) rootPath = fs::path(pfx + w);
    }
#endif

    // Collect regular files as (archive-relative name, disk path). Sort for a deterministic
    // archive (byte-stable output across builds — matters for a checked-in/diffable artifact).
    // NB: iteration errors must FAIL, never silently truncate — a partial .hky would ship as a
    // "successful" but broken bundle (this bit us: a MAX_PATH stop packed 93 of 39040 files).
    std::vector<std::pair<std::string, fs::path>> entries;
    fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec);
    if (ec) { err = "cannot iterate: " + dir + " (" + ec.message() + ")"; return false; }
    for (const fs::recursive_directory_iterator end; it != end; ) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            std::string rel = it->path().lexically_relative(rootPath).generic_string();
            if (!rel.empty() && rel[0] != '.')   // skip dotfiles / anything above the root
                entries.emplace_back(std::move(rel), it->path());
        }
        it.increment(ec);
        if (ec) { err = "iteration error under " + dir + ": " + ec.message(); return false; }
    }
    if (entries.empty()) { err = "empty tree: " + dir; return false; }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    fs::create_directories(fs::path(outHkyPath).parent_path(), ec);   // ensure plugins/ exists

    // Write to a sibling temp file and atomically rename on success — NEVER write outHkyPath in place.
    // A pack of the ~59k-file vanilla master is slow; if the process is killed mid-write (a build
    // timeout did exactly this), an in-place writer leaves a truncated-but-newer .hky that the build
    // treats as up-to-date and ships — an unreadable master → every graph loses its base unit → A-pose.
    // With temp+rename, an interrupted pack leaves only the throwaway .tmp; outHkyPath is untouched
    // (keeps the last-good bundle, or stays absent so the build re-packs). Same volume → the rename is
    // atomic; the finished bytes appear at outHkyPath all at once or not at all.
    const std::string tmpPath = outHkyPath + ".tmp";
    fs::remove(tmpPath, ec);   // clear any leftover from a prior interrupted pack

    mz_zip_archive z;
    std::memset(&z, 0, sizeof z);
    if (!mz_zip_writer_init_file(&z, tmpPath.c_str(), 0)) {
        err = "cannot create .hky (temp): " + tmpPath;
        return false;
    }
    for (const auto& [name, path] : entries) {
        std::ifstream f(path, std::ios::binary);
        std::string   bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!mz_zip_writer_add_mem(&z, name.c_str(), bytes.data(), bytes.size(), MZ_BEST_COMPRESSION)) {
            mz_zip_writer_end(&z);
            fs::remove(tmpPath, ec);
            err = "zip add failed: " + name;
            return false;
        }
    }
    if (!mz_zip_writer_finalize_archive(&z)) {
        mz_zip_writer_end(&z);
        fs::remove(tmpPath, ec);
        err = "zip finalize failed: " + tmpPath;
        return false;
    }
    mz_zip_writer_end(&z);

    // Promote temp -> final. Remove the old bundle first (Windows fs::rename won't overwrite), then
    // rename; the gap is a single already-finalized file move, not a multi-minute rewrite.
    fs::remove(outHkyPath, ec);
    std::error_code rec;
    fs::rename(tmpPath, outHkyPath, rec);
    if (rec) {
        fs::remove(tmpPath, ec);
        err = "cannot promote packed .hky to " + outHkyPath + " (" + rec.message() + ")";
        return false;
    }
    return true;
}

std::optional<std::string> HkyArchive::file(const std::string& path) const {
    const auto it = m_files.find(path);   // caller passes an already-normalized key
    if (it == m_files.end()) return std::nullopt;
    return it->second.content;
}

std::vector<std::string> HkyArchive::filesUnder(const std::string& prefix) const {
    std::vector<std::string> out;
    for (auto it = m_files.lower_bound(prefix); it != m_files.end() && it->first.rfind(prefix, 0) == 0; ++it)
        out.push_back(it->first);   // normalized (lowercase) — the default; unit keys + serve keys want this
    return out;
}

std::vector<std::string> HkyArchive::filesUnderOrig(const std::string& prefix) const {
    // Same range (scan by the normalized prefix over normalized keys) but return the ORIGINAL-CASE
    // paths. Only for callers that derive a case-sensitive NAME from the path — the animationdata clip
    // records, whose name the engine matches to the mixed-case behaviour clip generator. Read-back via
    // file()/read() re-normalizes, so the original case is safe to hand out.
    std::vector<std::string> out;
    for (auto it = m_files.lower_bound(prefix); it != m_files.end() && it->first.rfind(prefix, 0) == 0; ++it)
        out.push_back(it->second.orig);
    return out;
}

std::shared_ptr<const IUnitSource> HkyArchive::source(const std::string& unitPrefix) const {
    return std::make_shared<ZipUnitSource>(shared_from_this(), norm(unitPrefix));
}

}  // namespace havok::model
