#include "PCH.h"

#include "core/discover/BundleReader.h"

#include <havok/model/yaml/HkyArchive.h>
#include <havok/model/yaml/UnitSource.h>   // DiskUnitSource (unpacked-bundle unit backing)

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace CB {

    namespace {

        // lowercase + forward-slash — matches HkyArchive's stored keys and folds a caller's
        // backslashed/mixed-case rel path to the archive's key space.
        std::string Norm(std::string s)
        {
            for (char& c : s) {
                if (c == '\\') c = '/';
                else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        }

        // Normalize a directory prefix to HkyArchive's key space with EXACTLY one
        // trailing slash. Callers pass prefixes with OR without a trailing '/'
        // (e.g. "skeleton/" vs a built "<unit>/bones/"); appending '/' unconditionally
        // yields a "//" that never prefix-matches the single-slash keys — the base
        // skeleton "no bones found" bug (empty bones/ enumeration → empty bone table
        // → behavior compile fails → A-pose). Strip any trailing slashes, then add one.
        std::string DirBase(const std::string& dirPrefix)
        {
            std::string b = Norm(dirPrefix);
            while (!b.empty() && b.back() == '/') b.pop_back();
            b += '/';
            return b;
        }

        std::string SlurpFile(const fs::path& p)
        {
            std::ifstream f(p, std::ios::binary);
            if (!f) return {};
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        bool EndsWith(const std::string& s, const std::string& suf)
        {
            return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
        }

    }  // namespace

    std::optional<BundleReader> BundleReader::Open(const fs::path& bundle)
    {
        std::error_code ec;
        if (fs::is_directory(bundle, ec)) {
            BundleReader r;
            r.m_dir = bundle;
            return r;
        }
        // A regular .hky file -> packed archive (decompress once in memory).
        std::string err;
        auto arc = havok::model::HkyArchive::LoadFromFile(bundle.string(), err);
        if (!arc) return std::nullopt;
        BundleReader r;
        r.m_arc = std::move(arc);
        return r;
    }

    std::optional<std::string> BundleReader::read(const std::string& rel) const
    {
        if (m_arc) return m_arc->file(Norm(rel));
        const fs::path p = m_dir / fs::path(rel);
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) return std::nullopt;
        return SlurpFile(p);
    }

    std::vector<std::string> BundleReader::subdirs(const std::string& dirPrefix) const
    {
        std::vector<std::string> out;
        if (m_arc) {
            const std::string base = DirBase(dirPrefix);
            std::set<std::string> seen;   // dedup: many files share one child dir
            for (const auto& full : m_arc->filesUnder(base)) {
                const std::string rest = full.substr(base.size());   // "child/….txt" or "file.txt"
                const auto slash = rest.find('/');
                if (slash != std::string::npos) seen.insert(rest.substr(0, slash));  // has a subdir
            }
            out.assign(seen.begin(), seen.end());
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
            for (const auto& full : m_arc->filesUnder(base)) {
                const std::string rest = full.substr(base.size());
                if (rest.find('/') != std::string::npos) continue;   // deeper, not a direct child file
                if (!extLower.empty() && !EndsWith(rest, extLower)) continue;  // rest already lowercased
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
            if (!extLower.empty()) {
                std::string ext = it->path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext != extLower) continue;
            }
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
                if (extLower.empty() || EndsWith(full, extLower)) out.push_back(full);
            }
            return out;
        }
        std::error_code ec;
        const fs::path   d = m_dir / fs::path(dirPrefix);
        for (fs::recursive_directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            if (!extLower.empty()) {
                std::string ext = it->path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext != extLower) continue;
            }
            // whole-tree-relative (to the bundle root), forward-slashed + lowercased to match packed keys.
            out.push_back(Norm(it->path().lexically_relative(m_dir).generic_string()));
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
                std::string low = full;
                std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                if (extLower.empty() || EndsWith(low, extLower)) out.push_back(full);   // ext-check case-insensitive
            }
            return out;
        }
        std::error_code ec;
        const fs::path   d = m_dir / fs::path(dirPrefix);
        for (fs::recursive_directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            if (!extLower.empty()) {
                std::string ext = it->path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext != extLower) continue;
            }
            out.push_back(it->path().lexically_relative(m_dir).generic_string());   // case PRESERVED (no Norm)
        }
        return out;
    }

    std::vector<std::string> BundleReader::characterUnits() const
    {
        std::vector<std::string> out;
        if (m_arc) {
            for (const auto& u : m_arc->units())
                if (u.kind == havok::model::HkyArchive::UnitKind::Character) out.push_back(u.prefix);
            return out;
        }
        // Unpacked: walk for "<x>.hkx/character.yaml" dirs (a unit is a leaf — don't descend).
        std::error_code ec;
        for (fs::recursive_directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code de;
            if (!it->is_directory(de)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".hkx") continue;
            it.disable_recursion_pending();
            std::error_code ce;
            if (!fs::exists(it->path() / "character.yaml", ce)) continue;
            out.push_back(it->path().lexically_relative(m_dir).generic_string());
        }
        return out;
    }

    std::vector<std::string> BundleReader::behaviorUnits() const
    {
        std::vector<std::string> out;
        if (m_arc) {
            for (const auto& u : m_arc->units())
                if (u.kind == havok::model::HkyArchive::UnitKind::Behavior) out.push_back(u.prefix);
            return out;
        }
        // Unpacked: walk for "<x>.hkx/behavior.yaml" dirs (a unit is a leaf — don't descend).
        std::error_code ec;
        for (fs::recursive_directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code de;
            if (!it->is_directory(de)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".hkx") continue;
            it.disable_recursion_pending();
            std::error_code ce;
            if (!fs::exists(it->path() / "behavior.yaml", ce)) continue;
            out.push_back(it->path().lexically_relative(m_dir).generic_string());
        }
        return out;
    }

    std::shared_ptr<const havok::model::IUnitSource> BundleReader::unitSource(const std::string& prefix) const
    {
        if (m_arc) return m_arc->source(Norm(prefix));
        return std::make_shared<havok::model::DiskUnitSource>(m_dir / fs::path(prefix));
    }

}  // namespace CB
