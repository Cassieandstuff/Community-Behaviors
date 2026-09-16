#include "TreeDiffAdapters.h"

#include "TreeDiff.h"

#include "havok/sct/HavokFile.h"           // ReadHavokFile
#include "havok/sct/CharacterDecompiler.h" // DecompileToDir / DecompileResult (auto-routes behavior/character)
#include "havok/sct/BoneNames.h"          // BoneNameTable / ParseBoneList (--skeleton)
#include "havok/sct/SkeletonImport.h"     // LoadSkeletonsFromHkx / ReadSkeletonPhysics / SkeletonData
#include "havok/sct/SkeletonYaml.h"       // EmitSkeletonYamlTree
#include "havok/anim/AnimSetDataYaml.h"   // havok::animsetdata:: (setdata singlefile decompose)
#include "havok/anim/AnimDataYaml.h"      // havok::animdata::   (animdata singlefile decompose)

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace havok::diff {

namespace {

namespace fs = std::filesystem;

// Load a skeleton for bone-name resolution: a .hkx (SkeletonImport, first/animation skeleton) or a
// .txt bone list (one bone name per line). Returns the ordered bone names ({} on failure / no path).
// Lifted from havok-core-cli's LoadSkeletonNames — all public havok::sct surface.
std::vector<std::string> LoadSkeletonNames(const std::string& path, const LogFn& log) {
    if (path.empty()) return {};
    std::string lower = path;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".hkx") == 0) {
        std::vector<std::uint8_t> bytes; std::string err;
        if (!havok::sct::ReadHavokFile(path, bytes, &err)) { log("skeleton: " + err); return {}; }
        std::vector<havok::sct::SkeletonData> skels;
        if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
            log("skeleton: parse failed (" + err + ")"); return {};
        }
        std::vector<std::string> names;
        names.reserve(skels[0].bones.size());
        for (const auto& b : skels[0].bones) names.push_back(b.name);
        return names;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) { log("skeleton: cannot open " + path); return {}; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return havok::sct::ParseBoneList(text).names;
}

// Index -> name rosters parsed from a decompiled data/graphdata.yaml. CB and Pandora number the
// event/variable rosters in DIFFERENT order, so every raw *EventId / *variableIndex renumbers between
// them (same event, different int). Resolving those ints to names through EACH tree's own roster makes
// that churn vanish while a real event/variable REWIRE survives as a name change.
struct VocabRosters {
    std::vector<std::string> events;      // index -> event name
    std::vector<std::string> variables;   // index -> variable name
    bool empty() const { return events.empty() && variables.empty(); }
};

// Extract a scalar name token from a `- name: 'X'` line (handles quoted or bare).
std::string nameToken(const std::string& s) {
    std::size_t c = s.find("name:");
    if (c == std::string::npos) return {};
    std::string v = s.substr(c + 5);
    std::size_t b = v.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    v = v.substr(b);
    if (!v.empty() && (v[0] == '\'' || v[0] == '"')) {
        char q = v[0];
        std::size_t e = v.find(q, 1);
        return (e == std::string::npos) ? v.substr(1) : v.substr(1, e - 1);
    }
    std::size_t e = v.find_first_of(" \t\r\n");
    return v.substr(0, e);
}

VocabRosters ParseRosters(const fs::path& treeRoot) {
    VocabRosters r;
    std::ifstream f(treeRoot / "data" / "graphdata.yaml", std::ios::binary);
    if (!f) return r;
    std::string line;
    std::vector<std::string>* section = nullptr;   // current roster being filled
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] != ' ' && line[0] != '\t' && line[0] != '-') {   // top-level `key:`
            if (line.rfind("variables:", 0) == 0)   section = &r.variables;
            else if (line.rfind("events:", 0) == 0)  section = &r.events;
            else                                     section = nullptr;
            continue;
        }
        // a 2-space list item `  - name:` inside the active section starts a new entry
        if (section) {
            std::size_t b = line.find_first_not_of(" \t");
            if (b != std::string::npos && line[b] == '-' && line.find("name:") != std::string::npos)
                section->push_back(nameToken(line));
        }
    }
    return r;
}

// Does `key` name an event-index field (ends in "EventId") or a variable-index field?
bool isEventIdField(const std::string& k) {
    static const std::string suf = "EventId";
    return k.size() > suf.size() && k.compare(k.size() - suf.size(), suf.size(), suf) == 0;
}
bool isVarIndexField(const std::string& k) {
    return k == "variableIndex" || k == "syncVariableIndex" ||
           (k.size() > 13 && k.compare(k.size() - 13, 13, "VariableIndex") == 0);
}

// Rewrite each `<field>: <int>` whose field is an event/variable index into `<field>: '<name>'`,
// resolved through THIS tree's roster. -1 (no event/var) and out-of-range ints are left as-is.
std::string ResolveIndices(const std::string& yaml, const VocabRosters& r) {
    if (r.empty()) return yaml;
    std::string out;
    out.reserve(yaml.size() + 64);
    std::size_t i = 0;
    while (i < yaml.size()) {
        std::size_t eol = yaml.find('\n', i);
        std::string line = yaml.substr(i, eol == std::string::npos ? std::string::npos : eol - i);
        i = (eol == std::string::npos) ? yaml.size() : eol + 1;

        std::size_t colon = line.find(':');
        std::size_t k0 = line.find_first_not_of(" \t");
        if (colon != std::string::npos && k0 != std::string::npos && k0 < colon) {
            std::string key = line.substr(k0, colon - k0);
            const std::vector<std::string>* roster =
                isEventIdField(key) ? &r.events : (isVarIndexField(key) ? &r.variables : nullptr);
            if (roster) {
                std::string val = line.substr(colon + 1);
                std::size_t vb = val.find_first_not_of(" \t");
                std::size_t ve = val.find_last_not_of(" \t\r");
                if (vb != std::string::npos) {
                    std::string num = val.substr(vb, ve - vb + 1);
                    bool digits = !num.empty() && (num[0] == '-' || std::isdigit((unsigned char)num[0]));
                    for (std::size_t j = (num[0] == '-') ? 1 : 0; digits && j < num.size(); ++j)
                        if (!std::isdigit((unsigned char)num[j])) digits = false;
                    if (digits) {
                        long idx = std::atol(num.c_str());
                        if (idx >= 0 && static_cast<std::size_t>(idx) < roster->size())
                            line = line.substr(0, colon + 1) + " '" + (*roster)[idx] + "'";
                    }
                }
            }
        }
        out += line;
        out += '\n';
    }
    return out;
}

// Read a decompiled tree back into a RecordSet: each *.yaml keyed by its `id:` FIRST LINE
// (form `id: Class:name`) when present, else by its relative path. Never keyed by filename. When
// `rosters` is set (behavior domain), raw event/variable indices are resolved to names first.
void ReadTreeAsRecordSet(const fs::path& root, RecordSet& rs, const VocabRosters* rosters = nullptr) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".yaml" && ext != ".yml" && ext != ".txt") continue;
        std::ifstream f(p, std::ios::binary);
        if (!f) continue;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string key;
        // First line `id: <...>` -> the stable editorID; else fall back to the relative path.
        if (content.rfind("id:", 0) == 0) {
            std::size_t eol = content.find('\n');
            std::string first = content.substr(0, eol == std::string::npos ? content.size() : eol);
            std::string v = first.substr(3);
            std::size_t b = v.find_first_not_of(" \t\r");
            std::size_t e = v.find_last_not_of(" \t\r");
            if (b != std::string::npos) key = v.substr(b, e - b + 1);
        }
        if (key.empty()) key = fs::relative(p, root, ec).generic_string();
        rs[key] = rosters ? ResolveIndices(content, *rosters) : std::move(content);
    }
}

// behavior / character adapter: a .hkx -> RecordSet keyed by the decompiler's `id: Class:name`.
// DecompileToDir auto-routes the graph (behavior vs character) and emits the identical
// `id:`-keyed tree used everywhere else, so behavior and character share this one path.
bool DecompileRecordSet(const std::string& hkxPath, const std::string& skel, const char* tag,
                        RecordSet& rs, std::string& err, const LogFn& log) {
    std::vector<std::uint8_t> bytes;
    if (!havok::sct::ReadHavokFile(hkxPath, bytes, &err)) return false;
    static std::atomic<unsigned> seq{0};
    const fs::path tmp = fs::temp_directory_path() /
        ("cb_treediff_" + std::string(tag) + "_" + std::to_string(seq++));
    std::error_code ec; fs::remove_all(tmp, ec);
    havok::sct::BoneNameTable boneTable;
    if (!skel.empty()) { boneTable.names = LoadSkeletonNames(skel, log); boneTable.Reindex(); }
    const auto r = havok::sct::DecompileToDir(bytes, tmp, boneTable.empty() ? nullptr : &boneTable);
    if (!r.ok) { err = r.error; fs::remove_all(tmp, ec); return false; }
    // Resolve raw event/variable indices to names through this graph's own roster (behavior graphs
    // only — a character tree has no graphdata, so rosters come back empty and this is a no-op).
    const VocabRosters rosters = ParseRosters(tmp);
    ReadTreeAsRecordSet(tmp, rs, rosters.empty() ? nullptr : &rosters);
    fs::remove_all(tmp, ec);
    return true;
}

// The behavior DiffPolicy: the order-significant key-aligned arrays + volatile-field skips.
DiffPolicy BehaviorPolicy() {
    DiffPolicy p;
    // Order-significant, key-aligned (Havok evaluates these arrays in order; reorder is reported).
    p.orderedKeyedArrays["transitions"]         = {"event", "toStateId", "flags"};
    p.orderedKeyedArrays["wildcardTransitions"] = {"event", "toStateId", "flags"};
    p.orderedKeyedArrays["parsedTransitions"]   = {"event", "toStateId", "flags"};
    p.orderedKeyedArrays["entryTransitions"]    = {"event", "toStateId", "flags"};
    p.orderedKeyedArrays["triggers"]            = {"event", "localTime", "relativeToEndOfClip"};
    // Bindings: match by memberPath and field-diff, but binding ORDER is not significant (no reorder report).
    p.unorderedKeyedArrays["bindings"]          = {"memberPath"};
    // graphdata vocab: CB and Pandora order the rosters differently; match by name, not position.
    p.unorderedKeyedArrays["variables"]         = {"name"};
    p.unorderedKeyedArrays["events"]            = {"name"};
    // Notify-event arrays: match by the fired event; firing order is not significant.
    p.unorderedKeyedArrays["enterNotifyEvents"] = {"event"};
    p.unorderedKeyedArrays["exitNotifyEvents"]  = {"event"};
    // Volatile index fields: skipped when the companion name line agrees on both sides. `id` covers a
    // notify event's raw event index (it carries a companion `event:` name).
    p.volatileIndexFields = { {"eventId", "event"}, {"variableIndex", "variable"}, {"id", "event"} };
    p.skipNumericFallbackId = true;
    return p;
}

// setdata adapter: an animationsetdatasinglefile.txt -> RecordSet keyed project stem -> {movesets|crcs}.
bool SetdataRecordSet(const std::string& file, RecordSet& rs, std::string& err) {
    namespace asd = havok::animsetdata;
    std::ifstream f(file, std::ios::binary);
    if (!f) { err = "cannot open " + file; return false; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    asd::SingleFile sf;
    try { sf = asd::ParseSingleFile(text); }
    catch (const std::exception& e) { err = std::string("setdata parse: ") + e.what(); return false; }
    for (const auto& proj : sf.projects) {
        const std::string stem = asd::StemForHeader(proj.header);
        rs[stem + "|movesets"] = asd::EmitMovesetsYaml(proj);
        rs[stem + "|crcs"]     = asd::EmitSetdataCrcsYaml(proj);
    }
    return true;
}

// animdata adapter: an animationdatasinglefile.txt -> RecordSet keyed project stem -> clip/motion.
// Best-effort: clip files are emitted WITHOUT roster resolution (index-keyed body), which is
// deterministic and same-emitter on both sides. Roster-resolved clip NAMES would need a meshes dir.
bool AnimdataRecordSet(const std::string& file, RecordSet& rs, std::string& err) {
    namespace ad = havok::animdata;
    std::ifstream f(file, std::ios::binary);
    if (!f) { err = "cannot open " + file; return false; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ad::SingleFile sf;
    try { sf = ad::ParseSingleFile(text); }
    catch (const std::exception& e) { err = std::string("animdata parse: ") + e.what(); return false; }
    static const std::vector<std::string> kEmptyRoster;
    for (const auto& p : sf.projects) {
        if (!p.hasAnimData) continue;
        const std::string stem = ad::StemForProjectName(p.name);
        for (const auto& c : p.clips)
            rs[stem + "|clip|" + (c.name.empty() ? c.animIndex : c.name)] =
                ad::EmitClipYaml(c, kEmptyRoster, false);
        for (const auto& m : p.motions)
            rs[stem + "|motion|" + (m.animation.empty() ? m.animIndex : m.animation)] =
                ad::EmitMotionSidecar(m);
    }
    return true;
}

// skeleton adapter: a skeleton .hkx -> RecordSet keyed by relative tree path (bones/<name>.yaml).
bool SkeletonRecordSet(const std::string& hkxPath, RecordSet& rs, std::string& err) {
    std::vector<std::uint8_t> bytes;
    if (!havok::sct::ReadHavokFile(hkxPath, bytes, &err)) return false;
    std::vector<havok::sct::SkeletonData> skels;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, &err) || skels.empty()) {
        if (err.empty()) err = "no skeleton in " + hkxPath;
        return false;
    }
    std::string perr;
    havok::sct::ReadSkeletonPhysics(bytes.data(), bytes.size(), skels[0], &perr);  // best-effort physics
    static std::atomic<unsigned> seq{0};
    const fs::path tmp = fs::temp_directory_path() / ("cb_treediff_skel_" + std::to_string(seq++));
    std::error_code ec; fs::remove_all(tmp, ec);
    if (!havok::sct::EmitSkeletonYamlTree(skels[0], tmp, &err)) { fs::remove_all(tmp, ec); return false; }
    ReadTreeAsRecordSet(tmp, rs);
    fs::remove_all(tmp, ec);
    return true;
}

// Auto-detect the domain from A's extension/name; --domain overrides.
std::string DetectDomain(const std::string& a) {
    std::string low = a;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto ends = [&](const char* s){ std::string t=s; return low.size()>=t.size() && low.compare(low.size()-t.size(), t.size(), t)==0; };
    if (ends(".txt")) {
        if (low.find("setdata") != std::string::npos) return "setdata";
        return "animdata";   // animationdatasinglefile.txt / any other singlefile
    }
    if (ends(".hky")) return "behavior";   // (.hky handling is refused below)
    // .hkx or dir: default behavior; skeleton/character need an explicit domain.
    return "behavior";
}

// Build the RecordSet for one input under the chosen domain. Returns false + err on failure.
bool RecordSetFor(const std::string& domain, const std::string& input, const std::string& skel,
                  RecordSet& rs, std::string& err, const LogFn& log) {
    if (domain == "behavior") {
        std::string low = input;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low.size() >= 4 && low.compare(low.size()-4,4,".hky")==0) {
            err = ".hky behavior input is not yet supported by tree-diff — decompress with hky-unpack "
                  "and diff the per-unit .hkx, or use the .hkx path (the primary/tested case).";
            return false;
        }
        if (fs::is_directory(input)) {
            err = "a pre-decompiled directory is not accepted (the same-emitter invariant requires "
                  "decompiling from binary here) — pass the source .hkx.";
            return false;
        }
        return DecompileRecordSet(input, skel, "beh", rs, err, log);
    }
    if (domain == "setdata")   return SetdataRecordSet(input, rs, err);
    if (domain == "animdata")  return AnimdataRecordSet(input, rs, err);
    if (domain == "skeleton")  return SkeletonRecordSet(input, rs, err);
    if (domain == "character") return DecompileRecordSet(input, skel, "char", rs, err, log);
    err = "unknown domain '" + domain + "'";
    return false;
}

}  // namespace

TreeDiffOutcome RunTreeDiff(const TreeDiffOptions& opts, const LogFn& log) {
    TreeDiffOutcome out;
    auto say = [&](const std::string& s) { if (log) log(s); };

    if (opts.a.empty() || opts.b.empty()) {
        out.error = "tree-diff needs two inputs A and B";
        return out;
    }
    const std::string outDir = opts.deltaDir.empty() ? "tree_diff_delta" : opts.deltaDir;
    const std::string domain =
        (opts.domain.empty() || opts.domain == "auto") ? DetectDomain(opts.a) : opts.domain;
    say("tree-diff: domain=" + domain);

    RecordSet rsA, rsB;
    std::string err;
    if (!RecordSetFor(domain, opts.a, opts.skeleton, rsA, err, log)) {
        out.error = "A (domain=" + domain + "): " + err;
        say("ERROR: " + out.error);
        return out;
    }
    say("tree-diff: A -> " + std::to_string(rsA.size()) + " records");
    if (!RecordSetFor(domain, opts.b, opts.skeleton, rsB, err, log)) {
        out.error = "B (domain=" + domain + "): " + err;
        say("ERROR: " + out.error);
        return out;
    }
    say("tree-diff: B -> " + std::to_string(rsB.size()) + " records");

    const DiffPolicy policy = (domain == "behavior") ? BehaviorPolicy() : DiffPolicy{};
    DiffResult result = DiffRecordSets(rsA, rsB, policy);

    // Single-artifact inputs: the artifact-rel is "." (one RecordSet pair).
    std::error_code ec;
    fs::remove_all(outDir, ec);
    SummaryEntry entry;
    entry.compared = 0;
    for (const auto& [k, v] : rsA) if (rsB.count(k)) ++entry.compared;
    WriteArtifactDelta(outDir, domain, ".", result, entry);
    std::vector<SummaryEntry> entries{entry};
    const bool anyDiff = result.AnyDifference();
    WriteSummary(outDir, entries, anyDiff);

    out.ok              = true;
    out.anyDifference   = anyDiff;
    out.artifacts       = 1;
    out.comparedRecords = entry.compared;
    out.differing       = static_cast<int>(result.changed.size());
    out.onlyA           = static_cast<int>(result.onlyInA.size());
    out.onlyB           = static_cast<int>(result.onlyInB.size());
    out.summaryPath     = (fs::path(outDir) / "summary.yaml").string();

    say("tree-diff [" + domain + "]: compared " + std::to_string(out.comparedRecords) +
        ", only-A " + std::to_string(out.onlyA) + ", only-B " + std::to_string(out.onlyB) +
        ", differing " + std::to_string(out.differing) + " -> " + outDir +
        (anyDiff ? " (DIFFERENCES)" : " (identical)"));
    return out;
}

}  // namespace havok::diff
