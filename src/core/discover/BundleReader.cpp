#include "PCH.h"

#include "core/discover/BundleReader.h"

#include <interface/UnitSource.h>   // DiskUnitSource (unpacked-bundle unit backing)
#include <codec/resource/PathKey.h>
#include <disk/Disk.h>

#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace CB {

    namespace {

        using CB::engine::codec::resource::HasExt;
        using CB::engine::codec::resource::PathKey;

        // Normalize a directory prefix to HkyArchive's key space with EXACTLY one
        // trailing slash. Callers pass prefixes with OR without a trailing '/'
        // (e.g. "skeleton/" vs a built "<unit>/bones/"); appending '/' unconditionally
        // yields a "//" that never prefix-matches the single-slash keys — the base
        // skeleton "no bones found" bug (empty bones/ enumeration → empty bone table
        // → behavior compile fails → A-pose). Strip any trailing slashes, then add one.
        std::string DirBase(const std::string& dirPrefix)
        {
            std::string b = PathKey(dirPrefix);
            while (!b.empty() && b.back() == '/') b.pop_back();
            b += '/';
            return b;
        }

    }  // namespace

    std::optional<BundleReader> BundleReader::Open(const fs::path& bundle, const CB::engine::exec::TaskRunner* runner)
    {
        std::error_code ec;
        if (fs::is_directory(bundle, ec)) {
            BundleReader r;
            r.m_dir = bundle;
            return r;
        }
        // A regular .hky file -> packed archive (decompress once in memory).
        std::string err;
        auto arc = CB::engine::codec::HkyArchive::LoadFromFile(bundle.string(), err, runner);
        if (!arc) return std::nullopt;
        BundleReader r;
        r.m_arc = std::move(arc);
        return r;
    }

    std::optional<std::string> BundleReader::read(const std::string& rel) const
    {
        if (m_arc) return m_arc->file(PathKey(rel));
        const fs::path p = m_dir / fs::path(rel);
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) return std::nullopt;
        return CB::engine::disk::ReadFile(p).value_or(std::string());
    }

    std::vector<std::string> BundleReader::subdirs(const std::string& dirPrefix) const
    {
        std::vector<std::string> out;
        if (m_arc) {
            /// @note ORIGINAL-case names (a setdata project folder becomes the served header): the listing is by
            ///       key order, PathKey is length-preserving so the key offsets slice the original paths, and a
            ///       child seen under two spellings is listed once, as its first.
            const std::string base = DirBase(dirPrefix);
            std::set<std::string> seen;
            for (const auto& full : m_arc->filesUnderOrig(base)) {
                const std::string rest = full.substr(base.size());   // "Child/….txt" or "File.txt"
                const auto slash = rest.find('/');
                if (slash == std::string::npos) continue;
                std::string child = rest.substr(0, slash);
                if (seen.insert(PathKey(child)).second) out.push_back(std::move(child));
            }
            return out;
        }
        std::error_code ec;
        const fs::path d = m_dir / fs::path(dirPrefix);
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code de;
            if (it->is_directory(de)) out.push_back(it->path().filename().string());
        }
        return out;
    }

    std::vector<std::string> BundleReader::files(const std::string& dir, const std::string& extLower) const
    {
        std::vector<std::string> out;
        if (m_arc) {
            const std::string base = DirBase(dir);
            for (const auto& full : m_arc->filesUnderOrig(base)) {   // ORIGINAL-case names (see subdirs)
                const std::string rest = full.substr(base.size());
                if (rest.find('/') != std::string::npos) continue;   // deeper, not a direct child file
                if (!extLower.empty() && !HasExt(rest, extLower)) continue;
                out.push_back(rest);
            }
            return out;
        }
        std::error_code ec;
        const fs::path d = m_dir / fs::path(dir);
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            std::string name = it->path().filename().string();
            if (!extLower.empty() && !HasExt(name, extLower)) continue;
            out.push_back(std::move(name));
        }
        return out;
    }

    std::vector<std::string> BundleReader::filesUnder(const std::string& dirPrefix, const std::string& extLower) const
    {
        std::vector<std::string> out;
        if (m_arc) {
            const std::string base = DirBase(dirPrefix);
            for (const auto& full : m_arc->filesUnder(base)) {   // full = whole-tree-relative, lowercased
                if (extLower.empty() || HasExt(full, extLower)) out.push_back(full);
            }
            return out;
        }
        std::error_code ec;
        const fs::path   d = m_dir / fs::path(dirPrefix);
        for (fs::recursive_directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            if (!extLower.empty() && !HasExt(it->path().filename().string(), extLower)) continue;
            // whole-tree-relative (to the bundle root), forward-slashed + lowercased to match packed keys.
            out.push_back(PathKey(it->path().lexically_relative(m_dir).generic_string()));
        }
        return out;
    }

    // Like filesUnder, but ORIGINAL-CASE paths — for the animationdata clip/motion reads, whose NAME
    // (derived from the filename) is case-sensitive to the engine's behaviour-clip match. Packed: via
    // HkyArchive::filesUnderOrig. Disk: generic_string() already preserves case (only Norm lowercases).
    std::vector<std::string> BundleReader::filesUnderOrig(const std::string& dirPrefix, const std::string& extLower) const
    {
        std::vector<std::string> out;
        if (m_arc) {
            const std::string base = DirBase(dirPrefix);
            for (const auto& full : m_arc->filesUnderOrig(base)) {
                if (extLower.empty() || HasExt(full, extLower)) out.push_back(full);
            }
            return out;
        }
        std::error_code ec;
        const fs::path   d = m_dir / fs::path(dirPrefix);
        for (fs::recursive_directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            if (!extLower.empty() && !HasExt(it->path().filename().string(), extLower)) continue;
            out.push_back(it->path().lexically_relative(m_dir).generic_string());   // case PRESERVED (no Norm)
        }
        return out;
    }

    std::vector<std::string> BundleReader::units(CB::engine::codec::HkyArchive::UnitKind kind) const
    {
        std::vector<std::string> out;
        if (m_arc) {
            for (const auto& u : m_arc->units())
                if (u.kind == kind) out.push_back(u.prefix);
            return out;
        }
        std::error_code ec;
        for (fs::recursive_directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code de;
            if (!it->is_directory(de)) continue;
            if (!HasExt(it->path().filename().string(), ".hkx")) continue;
            it.disable_recursion_pending();
            std::error_code se;
            const std::string text = CB::engine::disk::ReadFile(it->path() / "graph.yaml").value_or(std::string());
            const auto k = fs::exists(it->path() / "bonelist.yaml", se) ? CB::engine::codec::HkyArchive::UnitKind::Skeleton
                                                                       : CB::engine::codec::HkyArchive::KindOfGraphYaml(text);
            if (k == kind)
                out.push_back(it->path().lexically_relative(m_dir).generic_string());
        }
        return out;
    }

    std::shared_ptr<const CB::engine::common::IUnitSource> BundleReader::unitSource(const std::string& prefix) const
    {
        if (m_arc) return m_arc->source(PathKey(prefix));
        return std::make_shared<CB::engine::common::DiskUnitSource>(m_dir / fs::path(prefix));
    }

}  // namespace CB
