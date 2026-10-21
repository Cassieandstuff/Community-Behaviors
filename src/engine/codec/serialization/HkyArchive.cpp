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

    std::vector<FileStamp> stamps() const override {
        std::vector<FileStamp> out;
        for (auto& e : m_arc->entriesUnder(m_root))
            out.push_back({ e.path.substr(m_root.size()), e.size, e.crc });   // m_files is ordered -> stable
        return out;
    }

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

/// @brief Every file entry of the zip's central directory, in archive order; nothing is inflated.
/// @return false with `err` set when the index can't be read, or an entry isn't stored/deflate (or is encrypted).
template <class OnEntry>
bool WalkIndex(const disk::ReadOnlyFile& file, const std::string& hkyPath, std::string& err, OnEntry&& onEntry) {
    mz_zip_archive z;
    std::memset(&z, 0, sizeof z);
    z.m_pRead      = [](void* f, mz_uint64 ofs, void* buf, std::size_t n) -> std::size_t {
        return static_cast<const disk::ReadOnlyFile*>(f)->ReadAt(ofs, buf, n) ? n : 0;
    };
    z.m_pIO_opaque = const_cast<disk::ReadOnlyFile*>(&file);
    if (!mz_zip_reader_init(&z, file.Size(), 0)) { err = "not a readable .hky (zip): " + hkyPath; return false; }
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; ++i) {
        if (mz_zip_reader_is_file_a_directory(&z, i)) continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) { mz_zip_reader_end(&z); err = "corrupt .hky (stat): " + hkyPath; return false; }
        if (st.m_is_encrypted || !st.m_is_supported || (st.m_method != 0 && st.m_method != MZ_DEFLATED)) {
            mz_zip_reader_end(&z);
            err = "unsupported entry '" + std::string(st.m_filename) + "' in " + hkyPath;
            return false;
        }
        onEntry(st);
    }
    mz_zip_reader_end(&z);
    return true;
}

}  // namespace

std::optional<std::vector<HkyArchive::IndexEntry>> HkyArchive::ReadIndex(const std::string& hkyPath, std::string& err) {
    const auto file = disk::ReadOnlyFile::Open(std::filesystem::path(hkyPath), &err);
    if (!file) return std::nullopt;
    std::vector<IndexEntry> out;
    if (!WalkIndex(*file, hkyPath, err, [&](const mz_zip_archive_file_stat& st) {
            out.push_back({ slashOnly(st.m_filename), st.m_uncomp_size, st.m_crc32 });
        }))
        return std::nullopt;
    return out;
}

std::shared_ptr<HkyArchive> HkyArchive::LoadFromFile(const std::string& hkyPath, std::string& err) {
    auto arc    = std::shared_ptr<HkyArchive>(new HkyArchive());
    arc->m_path = hkyPath;
    arc->m_file = disk::ReadOnlyFile::Open(std::filesystem::path(hkyPath), &err);
    if (!arc->m_file) return nullptr;
    const bool indexed = WalkIndex(*arc->m_file, hkyPath, err, [&](const mz_zip_archive_file_stat& st) {
        arc->m_files.emplace(PathKey(st.m_filename),
                             Entry{ slashOnly(st.m_filename), st.m_local_header_ofs, st.m_comp_size, st.m_uncomp_size,
                                    st.m_crc32, st.m_method == MZ_DEFLATED });
    });
    if (!indexed) return nullptr;
    if (arc->m_files.empty()) { err = ".hky is empty: " + hkyPath; return nullptr; }

    /// @note EVERY "<prefix>.hkx/..." subtree is a unit (a mod's patch unit may hold only changed nodes / rosters and
    ///       is still a real layer). m_files is sorted, so all files under one root are consecutive. A skeleton is
    ///       its own format (bonelist.yaml + bones/, the skeleton compiler's), not a reflective graph.
    try {
        std::string lastRoot;
        for (const auto& [k, _] : arc->m_files) {
            const auto pos = k.find(".hkx/");
            if (pos == std::string::npos) continue;
            std::string root = k.substr(0, pos + 4);
            if (root == lastRoot) continue;
            lastRoot = root;
            const auto g = arc->m_files.find(root + "/graph.yaml");
            const UnitKind kind = KindOfUnit(g == arc->m_files.end() ? std::nullopt : std::optional<std::string>(arc->read(g->second)),
                                             arc->m_files.count(root + "/bonelist.yaml") != 0);
            arc->m_units.push_back({ std::move(root), kind });
        }
    } catch (const std::exception& e) {
        err = e.what();
        return nullptr;
    }
    return arc;
}

HkyArchive::~HkyArchive() = default;

std::string HkyArchive::read(const Entry& e) const {
    const auto fault = [&](const char* what) { return std::runtime_error(m_path + ": '" + e.orig + "' " + what); };
    /// @note The data starts after the LOCAL header's name + extra fields, whose lengths may differ from the central
    ///       directory's — so the local header is read for them.
    unsigned char lh[30];
    if (!m_file->ReadAt(e.headerOffset, lh, sizeof lh)) throw fault("cannot be read");
    const auto le16 = [&](int at) { return static_cast<std::uint64_t>(lh[at] | (lh[at + 1] << 8)); };
    if (le16(0) != 0x4b50 || le16(2) != 0x0403) throw fault("has no local header");
    const std::uint64_t dataOffset = e.headerOffset + sizeof lh + le16(26) + le16(28);

    std::string out(static_cast<std::size_t>(e.size), '\0');
    if (!e.deflated) {
        if (e.packedSize != e.size || !m_file->ReadAt(dataOffset, out.data(), out.size())) throw fault("cannot be read");
    } else if (e.size > 0) {
        std::string packed(static_cast<std::size_t>(e.packedSize), '\0');
        if (!m_file->ReadAt(dataOffset, packed.data(), packed.size())) throw fault("cannot be read");
        if (tinfl_decompress_mem_to_mem(out.data(), out.size(), packed.data(), packed.size(), 0) != out.size())
            throw fault("does not inflate");
    }
    if (mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(out.data()), out.size()) != e.crc)
        throw fault("fails its CRC check");
    return out;
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

HkyArchive::UnitKind HkyArchive::KindOfUnit(const std::optional<std::string>& graphYaml, bool hasBonelist) {
    if (hasBonelist) return UnitKind::Skeleton;
    if (!graphYaml || IsPatchGraphYaml(*graphYaml)) return UnitKind::Patch;
    return KindOfGraphYaml(*graphYaml);
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
    std::vector<PackEntry> files;
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
            files.push_back({ std::move(name), [child = std::move(child)] { return disk::ReadFile(child); } });
        }
        if (we) { err = "cannot walk '" + d.string() + "': " + we.message(); return false; }
    }
    if (files.empty()) { err = "empty tree: " + dir; return false; }
    return PackEntries(std::move(files), outHkyPath, err);
}

bool HkyArchive::PackEntries(std::vector<PackEntry> files, const std::string& outHkyPath, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path out     = fs::absolute(fs::path(outHkyPath), ec);
    const fs::path partial = fs::path(out.native() + fs::path(".partial").native());
    if (files.empty()) { err = "nothing to pack: " + outHkyPath; return false; }

    /// @note Sorted: the entry order is deterministic, independent of where the entries came from.
    std::sort(files.begin(), files.end(), [](const PackEntry& a, const PackEntry& b) { return a.name < b.name; });
    /// @note Two entries naming one path (path identity, as every lookup resolves it) would make one of them
    ///       unreachable, so that fails the pack rather than shipping a shadowed file.
    {
        std::vector<std::pair<std::string, const std::string*>> keys;
        keys.reserve(files.size());
        for (const auto& f : files) keys.emplace_back(PathKey(f.name), &f.name);
        std::sort(keys.begin(), keys.end());
        for (std::size_t i = 1; i < keys.size(); ++i)
            if (keys[i].first == keys[i - 1].first) {
                err = "two entries name one path: '" + *keys[i - 1].second + "' and '" + *keys[i].second + "'";
                return false;
            }
    }

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
                auto bytes = files[start + i].read();
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
                const std::string& name = files[start + i].name;
                if (!d.read) return abandon("cannot read the source of '" + name + "' for packing");
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
    return read(it->second);
}

std::vector<std::string> HkyArchive::filesUnder(const std::string& prefix) const {
    std::vector<std::string> out;
    for (auto it = m_files.lower_bound(prefix); it != m_files.end() && it->first.rfind(prefix, 0) == 0; ++it)
        out.push_back(it->first);   // normalized (lowercase) — the default; unit keys + serve keys want this
    return out;
}

std::vector<HkyArchive::IndexEntry> HkyArchive::entriesUnder(const std::string& prefix) const {
    std::vector<IndexEntry> out;
    for (auto it = m_files.lower_bound(prefix); it != m_files.end() && it->first.rfind(prefix, 0) == 0; ++it)
        out.push_back({ it->second.orig, it->second.size, it->second.crc });
    return out;
}

std::optional<HkyArchive::IndexEntry> HkyArchive::entry(const std::string& path) const {
    const auto it = m_files.find(path);
    if (it == m_files.end()) return std::nullopt;
    return IndexEntry{ it->second.orig, it->second.size, it->second.crc };
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
