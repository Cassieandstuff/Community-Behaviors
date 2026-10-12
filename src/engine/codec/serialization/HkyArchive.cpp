#include <codec/serialization/HkyArchive.h>

#include <codec/resource/PathKey.h>
#include <common/miniz.h>
#include <disk/Disk.h>
#include <exec/Parallel.h>
#include <text/Text.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

namespace CB::engine::codec {
using namespace CB::engine::common;   // this layer operates on the interface data model

namespace {

using resource::PathKey;

struct MzFree {
    void operator()(void* p) const noexcept { mz_free(p); }
};

/// @brief `p` as miniz opens it: UTF-8 (miniz widens it for _wfopen on Windows).
std::string MzPath(const std::filesystem::path& p) {
    const std::u8string u = disk::LongPath(p).u8string();
    return std::string(u.begin(), u.end());
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
        auto s = m_arc->file(m_root + PathKey(rel));
        if (s) s->erase(0, s->size() - text::StripBom(*s).size());
        return s;
    }

    std::vector<std::string> listYaml(const std::string& subdir, bool recursive) const override {
        // m_root already ends in '/'. An EMPTY subdir (the Stage-2 whole-unit scan: listYaml("", true))
        // must NOT append another '/', or base becomes "<prefix>//" — a double slash that filesUnder's
        // prefix range can never match, so the scan returns ZERO node files and every packed graph
        // compiles nodeless (no rootGenerator/states/generators → char-setup null-deref on the first
        // graph the engine binds, i.e. the main-menu autoplay). The Stage-2 refactor moved from
        // per-section scans (non-empty subdir, no double slash) to this whole-unit scan; DiskUnitSource
        // handles "" correctly, so the disk-only acceptance gate never caught it.
        const std::string base = subdir.empty() ? m_root : (m_root + PathKey(subdir) + "/");
        std::vector<std::string> out;
        /// @note ORIGINAL-case paths: a reflective roster file is named for the field it holds
        ///       (`wildcardTransitions.transitions.yaml`), and Havok field names are case-sensitive — a lowercased
        ///       name binds nothing. read() re-normalizes, and PathKey() never changes length, so offsets line up.
        for (const auto& full : m_arc->filesUnderOrig(base)) {
            if (!endsWith(full, ".yaml")) continue;
            const std::string afterBase = full.substr(base.size());
            if (!recursive && afterBase.find('/') != std::string::npos) continue;
            out.push_back(full.substr(m_root.size()));   // unit-relative (e.g. "states/1.yaml")
        }
        return out;   // m_files is ordered -> deterministic
    }

    bool hasDir(const std::string& subdir) const override {
        return !m_arc->filesUnder(subdir.empty() ? m_root : (m_root + PathKey(subdir) + "/")).empty();
    }

private:
    std::shared_ptr<const HkyArchive> m_arc;
    std::string                       m_root;   // "<prefix>/"
};

}  // namespace

std::shared_ptr<HkyArchive> HkyArchive::LoadFromFile(const std::string& hkyPath, std::string& err,
                                                     const exec::TaskRunner* runner) {
    // Pass 1 (serial, cheap): enumerate every non-dir entry's index + name. Statting does NOT
    // decompress, so this is fast; the heavy inflate is pass 2.
    const std::string mzPath = MzPath(std::filesystem::path(hkyPath));
    mz_zip_archive    z;
    std::memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, mzPath.c_str(), 0)) { err = "cannot open .hky: " + hkyPath; return nullptr; }
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    struct EntRef { mz_uint idx; std::string key; std::string orig; };
    std::vector<EntRef> ents;
    ents.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        if (mz_zip_reader_is_file_a_directory(&z, i)) continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) { mz_zip_reader_end(&z); err = "corrupt .hky (stat)"; return nullptr; }
        ents.push_back({ i, PathKey(st.m_filename), slashOnly(st.m_filename) });
    }
    mz_zip_reader_end(&z);
    if (ents.empty()) { err = ".hky is empty: " + hkyPath; return nullptr; }

    auto arc = std::shared_ptr<HkyArchive>(new HkyArchive());

    /// @note Pass 2 inflates in parallel, one reader per WORKER (miniz is not thread-safe on one handle), each entry
    ///       into its own slot; m_files is then filled serially in entry order, so the result is independent of thread
    ///       count. Below a small floor a single worker does it all.
    constexpr std::size_t       kParallelFloor = 256;
    const exec::ParallelOptions popt{ .threads = ents.size() < kParallelFloor ? 1u : 0u,
                                      .runner  = ents.size() < kParallelFloor ? nullptr : runner };
    std::vector<mz_zip_archive> readers(exec::WorkerCount(ents.size(), popt));
    std::vector<char>           opened(readers.size(), 0);
    std::vector<std::string>    contents(ents.size());
    std::string                 fail;
    try {
        exec::ParallelFor(ents.size(), [&](std::size_t i, unsigned w) {
            if (!opened[w]) {
                std::memset(&readers[w], 0, sizeof readers[w]);
                if (!mz_zip_reader_init_file(&readers[w], mzPath.c_str(), 0))
                    throw std::runtime_error("cannot re-open .hky: " + hkyPath);
                opened[w] = 1;
            }
            std::size_t                   sz = 0;
            std::unique_ptr<void, MzFree> p(mz_zip_reader_extract_to_heap(&readers[w], ents[i].idx, &sz, 0));
            if (!p) throw std::runtime_error("extract failed: " + ents[i].orig);
            contents[i].assign(static_cast<const char*>(p.get()), sz);
        }, popt);
    } catch (const std::exception& e) {
        fail = e.what();
    }
    for (std::size_t w = 0; w < readers.size(); ++w)
        if (opened[w]) mz_zip_reader_end(&readers[w]);
    if (!fail.empty()) { err = std::move(fail); return nullptr; }
    for (std::size_t i = 0; i < ents.size(); ++i)
        arc->m_files.emplace(std::move(ents[i].key), Entry{ std::move(ents[i].orig), std::move(contents[i]) });

    /// @note EVERY "<prefix>.hkx/..." subtree is a unit (a mod's patch unit may hold only changed nodes / rosters and
    ///       is still a real layer). m_files is sorted, so all files under one root are consecutive. A skeleton is
    ///       its own format (bonelist.yaml + bones/, the skeleton compiler's), not a reflective graph.
    std::string lastRoot;
    for (const auto& [k, _] : arc->m_files) {
        const auto pos = k.find(".hkx/");
        if (pos == std::string::npos) continue;
        std::string root = k.substr(0, pos + 4);
        if (root == lastRoot) continue;
        lastRoot = root;
        const auto g = arc->m_files.find(root + "/graph.yaml");
        const UnitKind kind = arc->m_files.count(root + "/bonelist.yaml") ? UnitKind::Skeleton
                            : g == arc->m_files.end()                    ? UnitKind::Behavior
                                                                          : KindOfGraphYaml(g->second.content);
        arc->m_units.push_back({ std::move(root), kind });
    }
    if (arc->m_files.empty()) { err = ".hky is empty: " + hkyPath; return nullptr; }
    return arc;
}

HkyArchive::UnitKind HkyArchive::KindOfGraphYaml(std::string_view graphYaml) {
    if (graphYaml.find("'hkbCharacterData'") != std::string_view::npos)      return UnitKind::Character;
    if (graphYaml.find("'hkbProjectData'") != std::string_view::npos)        return UnitKind::Project;
    if (graphYaml.find("'hkaAnimationContainer'") != std::string_view::npos) return UnitKind::Skeleton;
    return UnitKind::Behavior;
}

bool HkyArchive::IsPatchGraphYaml(std::string_view graphYaml) {
    std::size_t v = 4;
    if (graphYaml.compare(0, 4, "id: ") != 0) {
        v = graphYaml.find("\nid: ");
        if (v == std::string_view::npos) return false;
        v += 5;
    }
    const std::size_t x = graphYaml.find_first_not_of("0123456789abcdefABCDEF", v);
    if (x == std::string_view::npos || graphYaml[x] != 'x' || x == v) return false;
    return graphYaml.substr(v, x - v).find_first_not_of('0') != std::string_view::npos;
}

bool HkyArchive::PackDirectory(const std::string& dir, const std::string& outHkyPath, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::absolute(fs::path(dir), ec);
    if (ec || !fs::is_directory(disk::LongPath(root), ec)) { err = "not a directory: " + dir; return false; }
    const fs::path out     = fs::absolute(fs::path(outHkyPath), ec);
    const fs::path partial = fs::path(out.native() + fs::path(".partial").native());

    /// @note Never self-add: when the output lands inside the tree, its (and its .partial's) archive name is skipped.
    ///       Compared by path identity — the filesystem's, not Havok data.
    std::vector<std::string> selfKeys;
    {
        const fs::path canonRoot = fs::weakly_canonical(root, ec);
        for (const fs::path& p : { out, partial }) {
            std::error_code ce;
            const fs::path rel = fs::weakly_canonical(p, ce).lexically_relative(canonRoot);
            if (!ce && !rel.empty() && *rel.begin() != "..") selfKeys.push_back(PathKey(rel.generic_string()));
        }
    }

    /// @note Every walk or stat error FAILS the pack: an incomplete archive must never pass as success (a MAX_PATH stop
    ///       once packed 93 of 39040 files; a skipped unit loads as nothing → A-pose, with no error anywhere).
    std::vector<std::pair<std::string, fs::path>> files;   ///< (archive name, disk path)
    for (std::vector<fs::path> stack{ root }; !stack.empty();) {
        const fs::path d = std::move(stack.back());
        stack.pop_back();
        std::error_code we;
        for (fs::directory_iterator it(disk::LongPath(d), we), end; !we && it != end; it.increment(we)) {
            fs::path   child = d / it->path().filename();
            const bool isDir = it->is_directory(we);
            if (we) break;
            if (isDir) { stack.push_back(std::move(child)); continue; }
            const bool isFile = it->is_regular_file(we);
            if (we) break;
            if (!isFile) continue;
            std::string name = child.lexically_relative(root).generic_string();
            if (!selfKeys.empty() && std::find(selfKeys.begin(), selfKeys.end(), PathKey(name)) != selfKeys.end()) continue;
            files.emplace_back(std::move(name), std::move(child));
        }
        if (we) { err = "cannot walk '" + d.string() + "': " + we.message(); return false; }
    }
    if (files.empty()) { err = "empty tree: " + dir; return false; }
    /// @note Sorted: the entry order is deterministic, independent of the filesystem's enumeration order.
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    /// @note Written to `<out>.partial` and renamed over `out` only once finalized: a killed or failed pack leaves the
    ///       previous archive (or none) at `out`, never a truncated-but-newer one a build would ship.
    fs::create_directories(disk::LongPath(out.parent_path()), ec);
    fs::remove(disk::LongPath(partial), ec);
    mz_zip_archive z;
    std::memset(&z, 0, sizeof z);
    if (!mz_zip_writer_init_file(&z, MzPath(partial).c_str(), 0)) { err = "cannot create: " + partial.string(); return false; }
    const auto abandon = [&](std::string why) {
        mz_zip_writer_end(&z);
        std::error_code re;
        fs::remove(disk::LongPath(partial), re);
        err = std::move(why);
        return false;
    };

    /// @note Deflate runs in parallel a batch at a time (bounded memory) into raw-deflate buffers; entries are then
    ///       written serially, in sorted order, as pre-compressed data. Default level: on YAML, level 9 costs several
    ///       times the CPU for a few percent of size, and a packed .hky is never byte-compared (diff the unpacked trees).
    struct Deflated {
        bool                          read = false;
        std::size_t                   size = 0;
        mz_uint32                     crc  = 0;
        std::unique_ptr<void, MzFree> data;   ///< raw deflate; null = stored
        std::size_t                   dataSize = 0;
        std::string                   raw;    ///< the bytes, when stored
    };
    const int             flags  = static_cast<int>(tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY));
    constexpr std::size_t kBatch = 2048;
    std::vector<Deflated> batch;
    try {
        for (std::size_t start = 0; start < files.size(); start += kBatch) {
            const std::size_t n = std::min(kBatch, files.size() - start);
            batch.clear();
            batch.resize(n);
            exec::ParallelFor(n, [&](std::size_t i) {
                auto bytes = disk::ReadFile(files[start + i].second);
                if (!bytes) return;
                Deflated& d = batch[i];
                d.read = true;
                d.size = bytes->size();
                d.crc  = static_cast<mz_uint32>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(bytes->data()), bytes->size()));
                if (!bytes->empty()) d.data.reset(tdefl_compress_mem_to_heap(bytes->data(), bytes->size(), &d.dataSize, flags));
                if (!d.data || d.dataSize >= d.size) {
                    d.data.reset();
                    d.raw = std::move(*bytes);
                }
            });
            for (std::size_t i = 0; i < n; ++i) {
                const Deflated&    d    = batch[i];
                const std::string& name = files[start + i].first;
                if (!d.read) return abandon("cannot read source file for packing: " + files[start + i].second.string());
                const bool good = d.data
                    ? mz_zip_writer_add_mem_ex(&z, name.c_str(), d.data.get(), d.dataSize, nullptr, 0, MZ_ZIP_FLAG_COMPRESSED_DATA, d.size, d.crc)
                    : mz_zip_writer_add_mem(&z, name.c_str(), d.raw.data(), d.raw.size(), MZ_NO_COMPRESSION);
                if (!good) return abandon("zip add failed: " + name);
            }
        }
    } catch (const std::exception& e) {
        return abandon(std::string("pack failed: ") + e.what());
    }
    if (!mz_zip_writer_finalize_archive(&z)) return abandon("zip finalize failed: " + partial.string());
    if (!mz_zip_writer_end(&z)) {
        fs::remove(disk::LongPath(partial), ec);
        err = "zip close failed: " + partial.string();
        return false;
    }
    fs::rename(disk::LongPath(partial), disk::LongPath(out), ec);
    if (ec) {
        err = "cannot replace '" + out.string() + "': " + ec.message();
        fs::remove(disk::LongPath(partial), ec);
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
    return std::make_shared<ZipUnitSource>(shared_from_this(), PathKey(unitPrefix));
}

}  // namespace CB::engine::codec
