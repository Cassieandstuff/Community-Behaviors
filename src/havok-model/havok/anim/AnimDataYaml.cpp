#include "havok/anim/AnimDataYaml.h"

#include <RymlInclude.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace havok::animdata {

namespace {

// Single-quoted YAML scalar — the motion value strings carry spaces (translation "t x y z") and
// backslashes (animationName "Animations\X.HKX"), so always quote to preserve them verbatim.
std::string Q(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "''"; else o += c; }
    o += "'";
    return o;
}

// A root-motion sample is "t x y z" (translation) or "t x y z w" (rotation). Present it with axis
// LABELS for readability — "t: <t>, x: <x>, …" — while keeping the float TOKENS verbatim (no
// parse/reformat), so the .txt round-trip stays byte-exact. Labels are chosen by token count so we
// never need to know which array we're in.
}  // namespace  (anon holds Q; LabelSample + UnlabelSample are exported — declared in AnimDataYaml.h)

// Exported (see header): labeled motion sample, so hky-utils' inline animation.yaml `motion:` emit
// matches EmitMotionSidecar byte-for-byte.
std::string LabelSample(const std::string& verbatim) {
    static const char* const kAxes[] = { "t", "x", "y", "z", "w" };
    std::vector<std::string> toks;
    std::string              cur;
    for (char c : verbatim) { if (c == ' ') { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } } else cur += c; }
    if (!cur.empty()) toks.push_back(cur);
    std::string out;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (i) out += ", ";
        if (i < 5) { out += kAxes[i]; out += ": "; }   // >5 tokens (never for motion): leave bare
        out += toks[i];
    }
    return out;
}

// Inverse of LabelSample, and tolerant of the legacy bare "t x y z" form (no labels): split on ','
// when labeled else on ' ', drop each "axis:" prefix, and rejoin the raw tokens with a single space
// — the canonical verbatim string the model + .txt carry. Idempotent on bare input.
std::string UnlabelSample(const std::string& s) {
    const char sep = (s.find(',') != std::string::npos) ? ',' : ' ';
    std::string out;
    bool        first = true;
    std::string cur;
    auto flush = [&] {
        auto b = cur.find_first_not_of(" \t");
        if (b != std::string::npos) {
            auto        e   = cur.find_last_not_of(" \t");
            std::string seg = cur.substr(b, e - b + 1);
            if (const auto colon = seg.find(':'); colon != std::string::npos) {   // strip "axis:" prefix
                auto vb = seg.find_first_not_of(" \t", colon + 1);
                seg     = (vb == std::string::npos) ? std::string() : seg.substr(vb);
            }
            if (!seg.empty()) { if (!first) out += ' '; out += seg; first = false; }
        }
        cur.clear();
    };
    for (char c : s) { if (c == sep) flush(); else cur += c; }
    flush();
    return out;
}

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
            y += "  - animation: " + Q(roster[static_cast<std::size_t>(idx)]) + "\n";
        else
            y += "  - index: " + m.animIndex + "\n";
        y += "    duration: " + Q(m.duration) + "\n";
        if (!m.translations.empty()) {
            y += "    translation:\n";
            for (const auto& t : m.translations) y += "      - " + Q(LabelSample(t)) + "\n";
        }
        if (!m.rotations.empty()) {
            y += "    rotation:\n";
            for (const auto& r : m.rotations) y += "      - " + Q(LabelSample(r)) + "\n";
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
            // legacy yaml. animIndex is resolved from the name at compile (ResolveMotionIndices).
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

std::size_t ResolveMotionIndices(std::vector<MotionRecord>& motions, const std::vector<ClipGenerator>& clips)
{
    // clip NAME -> animIndex (first occurrence wins — a name maps to one animation slot).
    std::unordered_map<std::string, std::string> byName;
    byName.reserve(clips.size() * 2);
    for (const auto& c : clips)
        if (!c.name.empty()) byName.emplace(c.name, c.animIndex);
    std::size_t unresolved = 0;
    for (auto& m : motions) {
        if (m.animation.empty()) continue;        // unnamed hybrid block: its animIndex is the key
        const auto it = byName.find(m.animation);
        if (it != byName.end()) m.animIndex = it->second;
        else ++unresolved;                         // authored motion for a clip that doesn't exist
    }
    return unresolved;
}

std::string EmitMotionSidecar(const MotionRecord& m)
{
    std::string y = "duration: " + Q(m.duration) + "\n";
    if (!m.translations.empty()) {
        y += "translation:\n";
        for (const auto& t : m.translations) y += "  - " + Q(LabelSample(t)) + "\n";
    }
    if (!m.rotations.empty()) {
        y += "rotation:\n";
        for (const auto& r : m.rotations) y += "  - " + Q(LabelSample(r)) + "\n";
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

    // Split on '/' or '\', collapsing '.' and '..', lowercasing each segment.
    std::vector<std::string> parts;
    std::string              seg;
    auto flush = [&] {
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        seg.clear();
    };
    for (char c : combined) {
        if (c == '/' || c == '\\') { flush(); continue; }
        seg += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    flush();

    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) { if (i) out += '/'; out += parts[i]; }
    return out;
}

// ── animationdatasinglefile.txt as a decomposed folder ───────────────────────────

namespace {
std::string LowerNoTxt(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".txt") == 0) s.erase(s.size() - 4);
    return s;
}
}  // namespace

std::string StemForProjectName(const std::string& projectName) { return LowerNoTxt(projectName); }

std::string UniqueFileName(const std::string& base, std::set<std::string>& used)
{
    auto norm = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();  // Windows strips these
        return s;
    };
    std::string stem = base;
    if (used.insert(norm(stem)).second) return stem;   // common case: no clash
    for (int n = 2; ; ++n) {
        stem = base + "__" + std::to_string(n);
        if (used.insert(norm(stem)).second) return stem;
    }
}

std::map<std::string, ProjectCharacter> LoadProjectCharacters(const std::string& treeMeshesDir)
{
    namespace fs = std::filesystem;
    auto lc = [](std::string s){ for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    auto strip = [](std::string s){
        while (!s.empty() && (s.back()=='\r'||s.back()=='\n'||s.back()==' '||s.back()=='\t')) s.pop_back();
        std::size_t b = 0; while (b < s.size() && (s[b]==' '||s[b]=='\t')) ++b;
        return s.substr(b);
    };
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
        if (lc(unit.extension().string()) != ".hkx") continue;   // a decomposed unit folder
        const std::string parent = lc(unit.parent_path().filename().string());
        const std::string stem   = lc(unit.stem().string());
        if (parent.rfind("characters", 0) == 0) {                // .../characters[ x]/<char>.hkx
            const fs::path ap = unit / "data" / "animations.yaml";
            if (!fs::exists(ap, ec)) continue;
            std::vector<std::string> roster;
            std::ifstream rf(ap); std::string line;
            while (std::getline(rf, line)) {   // block seq of single-quoted scalars: `- 'path'`
                std::string t = strip(line);
                if (t.empty() || t[0] == '#') continue;
                if (t.rfind("- ", 0) == 0) t = strip(t.substr(2));
                else if (!t.empty() && t[0] == '-') t = strip(t.substr(1));
                if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') {
                    const std::string inner = t.substr(1, t.size() - 2); std::string un;
                    for (std::size_t i = 0; i < inner.size(); ++i)
                        if (inner[i] == '\'' && i + 1 < inner.size() && inner[i + 1] == '\'') { un += '\''; ++i; }
                        else un += inner[i];
                    t = std::move(un);
                }
                if (!t.empty()) roster.push_back(std::move(t));
            }
            if (roster.empty()) continue;
            charByStem.emplace(stem, ProjectCharacter{ rel(unit), roster });
            charsByRoot[rel(unit.parent_path().parent_path())].push_back(stem);
        } else if (fs::exists(unit / "project.yaml", ec)) {      // a PROJECT unit at the actor root
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
        rootByFolder.emplace(slash == std::string::npos ? root : root.substr(slash + 1), root);
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
        y += "  - name: " + Q(p.name) + "\n";
        if (p.fieldX != "1") y += "    fieldX: " + Q(p.fieldX) + "\n";   // constant in vanilla; carry if odd
        if (!p.assetPaths.empty()) {
            y += "    assets:\n";
            for (const auto& a : p.assetPaths) y += "      - " + Q(a) + "\n";
        }
        if (p.hasAnimData) y += "    hasAnimData: true\n";
        // `character:` (roster source) only matters for projects with clips to resolve — skip the
        // header-only ones even if a same-named character exists.
        if (p.hasAnimData)
            if (const auto it = charRefByStem.find(StemForProjectName(p.name)); it != charRefByStem.end() && !it->second.empty())
                y += "    character: " + Q(it->second) + "\n";
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

std::string EmitClipYaml(const ClipGenerator& c, const std::vector<std::string>& roster, bool writeName)
{
    // The clip's NAME is the filename (not repeated here) UNLESS writeName — then the filename was
    // disambiguated (case-insensitive collision) and the true name is carried in the body. Key by the
    // ANIMATION it plays (roster[animIndex]) — resolvable back to the exact index — falling back to
    // `index:` when the index has no UNIQUE roster position (duplicate path / out-of-range / non-numeric).
    //
    // A caller that has ALREADY resolved the animation name off-roster (the converter's mod-delta path
    // joins a Nemesis clip to its behaviour clip-generator to get the animationName, and has no roster
    // — the index is a per-project bind the runtime does against the MERGED roster) passes it in
    // c.animation. Prefer that; it's the same field ParseClipYaml round-trips, and the base decompose
    // never sets it (it comes off the .txt with animation empty), so this only fires for pre-resolved
    // deltas. Shared animations (>1 clip, same animationName) round-trip correctly this way — the
    // roster path below would drop all but the first to `index:`.
    std::string animation = c.animation;
    if (animation.empty()) {
        char* end = nullptr;
        const long idx = std::strtol(c.animIndex.c_str(), &end, 10);
        if (end && *end == '\0' && idx >= 0 && static_cast<std::size_t>(idx) < roster.size()) {
            const std::string& path = roster[static_cast<std::size_t>(idx)];
            std::size_t first = static_cast<std::size_t>(-1);
            for (std::size_t i = 0; i < roster.size(); ++i) if (roster[i] == path) { first = i; break; }
            if (!path.empty() && first == static_cast<std::size_t>(idx)) animation = path;
        }
    }
    std::string y;
    if (writeName) y += "name: " + Q(c.name) + "\n";
    if (!animation.empty()) y += "animation: " + Q(animation) + "\n";
    else                    y += "index: " + Q(c.animIndex) + "\n";
    y += "speed: " + Q(c.playbackSpeed) + "\n";
    y += "cropStart: " + Q(c.cropStart) + "\n";
    y += "cropEnd: " + Q(c.cropEnd) + "\n";
    if (!c.triggers.empty()) {
        y += "triggers:\n";
        for (const auto& t : c.triggers) y += "  - " + Q(t) + "\n";
    }
    return y;
}

ClipGenerator ParseClipYaml(const std::string& text, std::string& err)
{
    ClipGenerator c;   // name filled only if the body carries `name:`; else caller sets from filename
    std::string storage = text;
    try {
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        auto root = tree.rootref();
        if (!root.readable() || !root.is_map()) return c;
        if (root.has_child(c4::to_csubstr("name")) && root[c4::to_csubstr("name")].has_val())
            c4::from_chars(root[c4::to_csubstr("name")].val(), &c.name);
        if (root.has_child(c4::to_csubstr("animation")) && root[c4::to_csubstr("animation")].has_val())
            c4::from_chars(root[c4::to_csubstr("animation")].val(), &c.animation);
        if (root.has_child(c4::to_csubstr("index")) && root[c4::to_csubstr("index")].has_val())
            c4::from_chars(root[c4::to_csubstr("index")].val(), &c.animIndex);
        if (root.has_child(c4::to_csubstr("speed")) && root[c4::to_csubstr("speed")].has_val())
            c4::from_chars(root[c4::to_csubstr("speed")].val(), &c.playbackSpeed);
        if (root.has_child(c4::to_csubstr("cropStart")) && root[c4::to_csubstr("cropStart")].has_val())
            c4::from_chars(root[c4::to_csubstr("cropStart")].val(), &c.cropStart);
        if (root.has_child(c4::to_csubstr("cropEnd")) && root[c4::to_csubstr("cropEnd")].has_val())
            c4::from_chars(root[c4::to_csubstr("cropEnd")].val(), &c.cropEnd);
        if (root.has_child(c4::to_csubstr("triggers")))
            for (auto t : root[c4::to_csubstr("triggers")]) {
                if (!t.has_val()) continue;
                std::string s; c4::from_chars(t.val(), &s); c.triggers.push_back(std::move(s));
            }
    } catch (const std::exception& e) {
        err = e.what();
    }
    return c;
}

std::size_t ResolveClipIndices(std::vector<ClipGenerator>& clips, const std::vector<std::string>& roster)
{
    // animation path -> first roster index. Built once per project.
    std::unordered_map<std::string, std::string> firstIndexOf;
    firstIndexOf.reserve(roster.size() * 2);
    for (std::size_t i = 0; i < roster.size(); ++i)
        firstIndexOf.emplace(roster[i], std::to_string(i));   // emplace keeps the FIRST occurrence
    std::size_t unresolved = 0;
    for (auto& c : clips) {
        if (c.animation.empty()) continue;          // raw-index fallback: its animIndex is the key
        const auto it = firstIndexOf.find(c.animation);
        if (it != firstIndexOf.end()) c.animIndex = it->second;
        else ++unresolved;                           // authored clip for an animation not in the roster
    }
    return unresolved;
}

void SortAnimdata(SingleFile& sf, bool sortProjects)
{
    auto lc = [](std::string s){ for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    auto idxLess = [](const std::string& a, const std::string& b) {
        char* ea = nullptr; char* eb = nullptr;
        const long ia = std::strtol(a.c_str(), &ea, 10);
        const long ib = std::strtol(b.c_str(), &eb, 10);
        const bool na = ea && *ea == '\0', nb = eb && *eb == '\0';
        if (na && nb) return ia < ib;    // both numeric: by value
        if (na != nb) return na;         // numeric before symbolic
        return a < b;                    // both symbolic: lexicographic
    };
    for (auto& p : sf.projects) {
        std::stable_sort(p.clips.begin(), p.clips.end(),
            [&](const ClipGenerator& a, const ClipGenerator& b){ return lc(a.name) < lc(b.name); });
        std::stable_sort(p.motions.begin(), p.motions.end(),
            [&](const MotionRecord& a, const MotionRecord& b){ return idxLess(a.animIndex, b.animIndex); });
    }
    // PROJECT order is LOAD-BEARING for the served cache: the engine binds each actor to its
    // animationdata block by project INDEX (parallel to animationsetdata), so reordering the project
    // list points every actor at the wrong block -> clips don't resolve (A-pose) + motion is garbage
    // (root-motion teleport). Only sort projects for the CLI's order-independent multiset GATE
    // (sortProjects=true); the compose path (AssembleAnimdata) MUST keep the source index.yaml order.
    if (sortProjects)
        std::stable_sort(sf.projects.begin(), sf.projects.end(),
            [&](const Project& a, const Project& b){ return lc(a.name) < lc(b.name); });
}

SingleFile AssembleAnimdata(
    const std::vector<ProjectHeader>& headers,
    const std::map<std::string, std::vector<ClipGenerator>>& clipsByStem,
    const std::map<std::string, std::vector<MotionRecord>>& motionByStem,
    const std::map<std::string, std::vector<std::string>>& rostersByStem)
{
    SingleFile out;
    out.projects.reserve(headers.size());
    for (const auto& h : headers) {
        Project p;
        p.name        = h.name;
        p.fieldX      = h.fieldX;
        p.assetPaths  = h.assets;
        p.hasAnimData = h.hasAnimData;
        if (h.hasAnimData) {
            const std::string stem = StemForProjectName(h.name);
            if (const auto c = clipsByStem.find(stem); c != clipsByStem.end()) p.clips = c->second;
            if (const auto m = motionByStem.find(stem); m != motionByStem.end()) p.motions = m->second;
            if (const auto r = rostersByStem.find(stem); r != rostersByStem.end())
                ResolveClipIndices(p.clips, r->second);  // animation -> animIndex (roster join)
            ResolveMotionIndices(p.motions, p.clips);    // clip name -> animIndex (clips join)
        }
        out.projects.push_back(std::move(p));
    }
    // Sort clips/motions WITHIN each project (inert — name/index lookup), but PRESERVE the project
    // order from `headers` (the source index.yaml = vanilla order). The project index is load-bearing.
    SortAnimdata(out, /*sortProjects=*/false);
    return out;
}

}  // namespace havok::animdata
