#include <codec/format/AnimDataYaml.h>

#include <codec/resource/PathKey.h>       // path identity (PathKey / HasExt)
#include <interface/MotionSample.h>      // Label/UnlabelSample (the sample-label codec, now in interface)
#include <RymlInclude.h>
#include <text/Text.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace CB::engine::animdata {

using CB::engine::codec::resource::HasExt;
using CB::engine::codec::resource::PathKey;

/// @note Scalars are always single-quoted: motion values carry spaces (translation "t x y z") and backslashes
///       (animationName "Animations\X.HKX"), and quoting keeps them verbatim.
std::string EmitMotionYaml(const Project& project, const std::vector<std::string>& roster)
{
    std::string y = "motions:\n";
    for (const auto& m : project.motions) {
        const long idx = std::atol(m.animIndex.c_str());
        // Key on the clip NAME when we have one (the vast majority): the animIndex is derivable from
        // it at compile, so shipping it would be a magic number waiting to drift. Fall back to the
        // raw index only for the unnamed hybrid blocks (mounted combat) the Havok assets don't name.
        const bool named = idx >= 0 && static_cast<std::size_t>(idx) < roster.size()
                           && !roster[static_cast<std::size_t>(idx)].empty();
        if (named)
            y += "  - animation: " + text::QuoteSingle(roster[static_cast<std::size_t>(idx)]) + "\n";
        else
            y += "  - index: " + m.animIndex + "\n";
        y += "    duration: " + text::QuoteSingle(m.duration) + "\n";
        if (!m.translations.empty()) {
            y += "    translation:\n";
            for (const auto& t : m.translations) y += "      - " + text::QuoteSingle(LabelSample(t)) + "\n";
        }
        if (!m.rotations.empty()) {
            y += "    rotation:\n";
            for (const auto& r : m.rotations) y += "      - " + text::QuoteSingle(LabelSample(r)) + "\n";
        }
    }
    return y;
}

std::vector<MotionRecord> ParseMotionYaml(const std::string& text, std::string& err)
{
    std::vector<MotionRecord> out;
    std::string storage = text;
    try {
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        auto root = tree.rootref();
        if (!root.readable() || !root.is_map() || !root.has_child(c4::to_csubstr("motions")))
            return out;
        for (auto mn : root[c4::to_csubstr("motions")]) {
            MotionRecord m;
            // Prefer the clip NAME (new form); the raw index is a fallback for unnamed blocks and
            // legacy yaml. animIndex is resolved from the name at compile.
            if (mn.has_child(c4::to_csubstr("animation")) && mn[c4::to_csubstr("animation")].has_val())
                c4::from_chars(mn[c4::to_csubstr("animation")].val(), &m.animation);
            if (mn.has_child(c4::to_csubstr("index")) && mn[c4::to_csubstr("index")].has_val())
                c4::from_chars(mn[c4::to_csubstr("index")].val(), &m.animIndex);
            if (mn.has_child(c4::to_csubstr("duration")) && mn[c4::to_csubstr("duration")].has_val())
                c4::from_chars(mn[c4::to_csubstr("duration")].val(), &m.duration);
            if (mn.has_child(c4::to_csubstr("translation")))
                for (auto t : mn[c4::to_csubstr("translation")]) {
                    if (!t.has_val()) continue;
                    std::string s; c4::from_chars(t.val(), &s); m.translations.push_back(UnlabelSample(s));
                }
            if (mn.has_child(c4::to_csubstr("rotation")))
                for (auto r : mn[c4::to_csubstr("rotation")]) {
                    if (!r.has_val()) continue;
                    std::string s; c4::from_chars(r.val(), &s); m.rotations.push_back(UnlabelSample(s));
                }
            out.push_back(std::move(m));
        }
    } catch (const std::exception& e) {
        err = e.what();
    }
    return out;
}

std::string EmitMotionSidecar(const MotionRecord& m)
{
    std::string y = "duration: " + text::QuoteSingle(m.duration) + "\n";
    if (!m.translations.empty()) {
        y += "translation:\n";
        for (const auto& t : m.translations) y += "  - " + text::QuoteSingle(LabelSample(t)) + "\n";
    }
    if (!m.rotations.empty()) {
        y += "rotation:\n";
        for (const auto& r : m.rotations) y += "  - " + text::QuoteSingle(LabelSample(r)) + "\n";
    }
    return y;
}

MotionRecord ParseMotionSidecar(const std::string& text, std::string& err)
{
    MotionRecord m;
    std::string  storage = text;
    try {
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        auto          root = tree.rootref();
        if (!root.readable() || !root.is_map()) return m;
        if (root.has_child(c4::to_csubstr("duration")) && root[c4::to_csubstr("duration")].has_val())
            c4::from_chars(root[c4::to_csubstr("duration")].val(), &m.duration);
        if (root.has_child(c4::to_csubstr("translation")))
            for (auto t : root[c4::to_csubstr("translation")]) {
                if (!t.has_val()) continue;
                std::string s; c4::from_chars(t.val(), &s); m.translations.push_back(UnlabelSample(s));
            }
        if (root.has_child(c4::to_csubstr("rotation")))
            for (auto rr : root[c4::to_csubstr("rotation")]) {
                if (!rr.has_val()) continue;
                std::string s; c4::from_chars(rr.val(), &s); m.rotations.push_back(UnlabelSample(s));
            }
    } catch (const std::exception& e) {
        err = e.what();
    }
    return m;
}

std::string CanonicalAnimPath(const std::string& actorRoot, const std::string& rosterEntry)
{
    std::string combined = actorRoot;
    if (!combined.empty() && combined.back() != '/' && combined.back() != '\\') combined += '/';
    combined += rosterEntry;

    // Split on '/' (PathKey folds '\' and case), collapsing '.' and '..'.
    std::vector<std::string> parts;
    std::string              seg;
    auto flush = [&] {
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        seg.clear();
    };
    for (char c : PathKey(combined)) {
        if (c == '/') { flush(); continue; }
        seg += c;
    }
    flush();

    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) { if (i) out += '/'; out += parts[i]; }
    return out;
}

// ── animationdatasinglefile.txt as a decomposed folder ───────────────────────────

std::string StemForProjectName(const std::string& projectName)
{
    std::string s = PathKey(projectName);
    if (HasExt(s, ".txt")) s.erase(s.size() - 4);
    return s;
}

std::map<std::string, ProjectCharacter> LoadProjectCharacters(
    const std::string& treeMeshesDir,
    const std::function<std::vector<std::string>(const std::filesystem::path& unit)>& rosterOf,
    const std::function<bool(const std::filesystem::path& unit)>& isProject)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base(treeMeshesDir);
    auto rel = [&](const fs::path& p){ return p.lexically_relative(base).generic_string(); };

    std::map<std::string, ProjectCharacter> charByStem;          // char stem -> {ref, roster}
    std::map<std::string, std::vector<std::string>> charsByRoot; // actor root(rel) -> [char stems]
    std::map<std::string, std::string> projRoot;                 // project stem -> actor root(rel)
    for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const fs::path unit = it->path();
        if (PathKey(unit.extension().string()) != ".hkx") continue;   // a decomposed unit folder
        const std::string parent = PathKey(unit.parent_path().filename().string());
        const std::string stem   = PathKey(unit.stem().string());
        if (parent.rfind("characters", 0) == 0) {                // .../characters[ x]/<char>.hkx
            std::vector<std::string> roster = rosterOf(unit);
            if (roster.empty()) continue;
            charByStem.emplace(stem, ProjectCharacter{ rel(unit), roster });
            charsByRoot[rel(unit.parent_path().parent_path())].push_back(stem);
        } else if (isProject(unit)) {                            // a PROJECT unit at the actor root
            projRoot.emplace(stem, rel(unit.parent_path()));
        }
    }

    auto charInRoot = [&](const std::string& root, const std::string& want) -> const ProjectCharacter* {
        const auto rit = charsByRoot.find(root);
        if (rit == charsByRoot.end() || rit->second.empty()) return nullptr;
        for (const auto& cs : rit->second) if (cs == want) return &charByStem[cs];   // prefer a name match
        return &charByStem[rit->second.front()];                                     // else the sole/first
    };
    std::map<std::string, std::string> rootByFolder;             // actor folder name -> actor root
    for (const auto& [root, chars] : charsByRoot) {
        const auto slash = root.find_last_of('/');
        rootByFolder.emplace(PathKey(slash == std::string::npos ? root : root.substr(slash + 1)), root);
    }

    std::map<std::string, ProjectCharacter> out;
    std::set<std::string> stems;
    for (auto& kv : charByStem)   stems.insert(kv.first);
    for (auto& kv : projRoot)     stems.insert(kv.first);
    for (auto& kv : rootByFolder) stems.insert(kv.first);
    for (const std::string& p : stems) {
        if (const auto c = charByStem.find(p); c != charByStem.end()) { out.emplace(p, c->second); continue; }  // (1) char name
        if (const auto r = projRoot.find(p); r != projRoot.end())                                               // (2) project unit
            if (const auto* c = charInRoot(r->second, p)) { out.emplace(p, *c); continue; }
        bool done = false;                                                                                      // (3) name variants
        for (const std::string& v : { p + "project", p + "data" })
            if (const auto c = charByStem.find(v); c != charByStem.end()) { out.emplace(p, c->second); done = true; break; }
        if (done) continue;
        for (const std::string& v : { p, p + "project", p + "data" }) {                                         // (4) actor folder name
            const auto r = rootByFolder.find(v);
            if (r != rootByFolder.end()) if (const auto* c = charInRoot(r->second, p)) { out.emplace(p, *c); break; }
        }
    }
    return out;
}

std::string EmitAnimdataIndexYaml(const SingleFile& sf,
                                  const std::map<std::string, std::string>& charRefByStem)
{
    std::string y = "# animationdatasinglefile.txt project manifest: name + assets + hasAnimData +\n";
    y += "# character (the roster a project's clip indices key into). Cache order is inert (the engine\n";
    y += "# name-binds), so this list's order is not load-bearing. Header-only projects live entirely\n";
    y += "# here; hasAnimData projects also carry clips/<stem>/*.yaml + motion/<stem>/*.yaml bodies.\n";
    y += "projects:\n";
    for (const auto& p : sf.projects) {
        y += "  - name: " + text::QuoteSingle(p.name) + "\n";
        if (p.fieldX != "1") y += "    fieldX: " + text::QuoteSingle(p.fieldX) + "\n";   // constant in vanilla; carry if odd
        if (!p.assetPaths.empty()) {
            y += "    assets:\n";
            for (const auto& a : p.assetPaths) y += "      - " + text::QuoteSingle(a) + "\n";
        }
        if (p.hasAnimData) y += "    hasAnimData: true\n";
        // `character:` (roster source) only matters for projects with clips to resolve — skip the
        // header-only ones even if a same-named character exists.
        if (p.hasAnimData)
            if (const auto it = charRefByStem.find(StemForProjectName(p.name)); it != charRefByStem.end() && !it->second.empty())
                y += "    character: " + text::QuoteSingle(it->second) + "\n";
    }
    return y;
}

std::vector<ProjectHeader> ParseAnimdataIndexYaml(const std::string& text, std::string& err)
{
    std::vector<ProjectHeader> out;
    std::string storage = text;
    try {
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        auto root = tree.rootref();
        if (!root.readable() || !root.is_map() || !root.has_child(c4::to_csubstr("projects")))
            return out;
        for (auto pn : root[c4::to_csubstr("projects")]) {
            ProjectHeader h;
            if (pn.has_child(c4::to_csubstr("name")) && pn[c4::to_csubstr("name")].has_val())
                c4::from_chars(pn[c4::to_csubstr("name")].val(), &h.name);
            if (pn.has_child(c4::to_csubstr("fieldX")) && pn[c4::to_csubstr("fieldX")].has_val())
                c4::from_chars(pn[c4::to_csubstr("fieldX")].val(), &h.fieldX);
            if (pn.has_child(c4::to_csubstr("assets")))
                for (auto a : pn[c4::to_csubstr("assets")]) {
                    if (!a.has_val()) continue;
                    std::string s; c4::from_chars(a.val(), &s); h.assets.push_back(std::move(s));
                }
            if (pn.has_child(c4::to_csubstr("hasAnimData")) && pn[c4::to_csubstr("hasAnimData")].has_val()) {
                std::string v; c4::from_chars(pn[c4::to_csubstr("hasAnimData")].val(), &v);
                h.hasAnimData = (v == "true" || v == "1");
            }
            if (pn.has_child(c4::to_csubstr("character")) && pn[c4::to_csubstr("character")].has_val())
                c4::from_chars(pn[c4::to_csubstr("character")].val(), &h.character);
            if (!h.name.empty()) out.push_back(std::move(h));
        }
    } catch (const std::exception& e) {
        err = e.what();
    }
    return out;
}

}  // namespace CB::engine::animdata
