#include "TreeDiffAdapters.h"

#include "TreeDiff.h"

#include "havok/sct/HavokFile.h"           // ReadHavokFile
#include "havok/sct/CharacterDecompiler.h" // DecompileToDir / DecompileResult (auto-routes behavior/character)
#include "havok/sct/BoneNames.h"          // BoneNameTable / ParseBoneList (--skeleton)
#include "havok/sct/SkeletonImport.h"     // LoadSkeletonsFromHkx / ReadSkeletonPhysics / SkeletonData
#include "havok/sct/SkeletonYaml.h"       // EmitSkeletonYamlTree
#include "havok/anim/AnimSetDataYaml.h"   // havok::animsetdata:: (setdata singlefile decompose)
#include "havok/anim/AnimDataYaml.h"      // havok::animdata::   (animdata singlefile decompose)
#include <havok-model/HavokModel.h>       // ParseTagfile / EmitHky / EmitFullBaseScaffolding (tagfile-XML diff)
#include <havok-schema/HavokSchema.h>     // SchemaRegistry (tagfile-XML parse needs the class descriptors)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
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

// The record's own TOP-LEVEL `class:`/`name:` (column-0 lines only — indented ones belong to nested
// sub-objects) and the numeric `id:` first line. The stable diff key is `Class:name` when a name exists
// (survives the #NNNN renumber between two independent compiles), else the numeric id, else the path.
void deriveKeyAndId(const std::string& content, std::string& key, std::string& ownId) {
    std::string cls, nm;
    std::size_t ls = 0;
    while (ls < content.size()) {
        std::size_t eol = content.find('\n', ls);
        const std::string line = content.substr(ls, eol == std::string::npos ? std::string::npos : eol - ls);
        ls = (eol == std::string::npos) ? content.size() : eol + 1;
        if (line.rfind("id:", 0) == 0 && ownId.empty()) {
            std::string v = line.substr(3);
            const std::size_t b = v.find_first_not_of(" \t\r"), e = v.find_last_not_of(" \t\r");
            if (b != std::string::npos) ownId = v.substr(b, e - b + 1);
            continue;
        }
        if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '-') continue;  // top-level only
        if (cls.empty() && line.rfind("class:", 0) == 0) {
            std::string v = line.substr(6);
            const std::size_t b = v.find_first_not_of(" \t\r"), e = v.find_last_not_of(" \t\r");
            if (b != std::string::npos) cls = v.substr(b, e - b + 1);
        } else if (nm.empty() && line.rfind("name:", 0) == 0) {
            nm = nameToken(line);
        }
    }
    if (!cls.empty() && !nm.empty()) key = cls + ":" + nm;
    else if (!ownId.empty())        key = ownId;
}

// A field name that holds a NODE REF (a graph edge). Bare ints are ambiguous — `pGenerator: 1325` is a
// ref but `limitHeadingDegrees: 90` / `startBoneIndex: 35` are scalars that can coincidentally equal a
// node id — so resolution is gated to ref-named fields only: anything containing "enerator"
// (generator/pGenerator/pDefaultGenerator/generators) or "odifier" (modifier/pModifier/modifiers). That
// covers the graph-edge churn that renumbers between compiles without touching scalars/indices/counts.
bool isRefFieldName(const std::string& name) {
    return name.find("enerator") != std::string::npos || name.find("odifier") != std::string::npos;
}

// Rewrite bare-int node refs -> the target's stable Class:name (renumber-immune cross-compile diff).
// Only ref-named `field: <int>` and bare-int list items `- <int>` under a ref-named container are touched.
std::string resolveNodeRefs(const std::string& yaml, const std::unordered_map<std::string, std::string>& idToName) {
    if (idToName.empty()) return yaml;
    std::string out;
    out.reserve(yaml.size() + 128);
    std::string listKey;   // most recent `key:`-with-no-value container (for `- <int>` items)
    std::size_t i = 0;
    while (i < yaml.size()) {
        std::size_t eol = yaml.find('\n', i);
        std::string line = yaml.substr(i, eol == std::string::npos ? std::string::npos : eol - i);
        i = (eol == std::string::npos) ? yaml.size() : eol + 1;

        const std::size_t k0 = line.find_first_not_of(" \t");
        bool resolve = false;
        std::size_t vpos = std::string::npos;
        if (k0 != std::string::npos && line[k0] == '-') {
            // list item: `- <int>` resolves only under a ref-named container.
            vpos = line.find_first_not_of(" \t", k0 + 1);
            resolve = isRefFieldName(listKey);
        } else if (const std::size_t c = line.find(':'); c != std::string::npos && k0 != std::string::npos && k0 < c) {
            const std::string field = line.substr(k0, c - k0);
            const std::size_t after = line.find_first_not_of(" \t", c + 1);
            if (after == std::string::npos) { listKey = field; }   // `key:` container line
            else { vpos = after; resolve = isRefFieldName(field); }
        }
        if (resolve && vpos != std::string::npos) {
            std::string val = line.substr(vpos);
            if (const std::size_t e = val.find_last_not_of(" \t\r"); e != std::string::npos) val.resize(e + 1);
            bool isInt = !val.empty();
            for (char c : val) if (!std::isdigit(static_cast<unsigned char>(c))) { isInt = false; break; }
            if (isInt) {
                const auto it = idToName.find(val);
                if (it != idToName.end() && it->second != val)
                    line = line.substr(0, vpos) + it->second;
            }
        }
        out += line;
        out += '\n';
    }
    return out;
}

// Read a decompiled tree back into a RecordSet. Two passes: (1) key every *.yaml by its stable
// Class:name (numeric id / path fallback) and build the id->name map; (2) resolve node refs + roster
// indices to names and drop the volatile top-level id line, so the compared body is renumber-immune.
void ReadTreeAsRecordSet(const fs::path& root, RecordSet& rs, const VocabRosters* rosters = nullptr) {
    std::error_code ec;
    struct Rec { std::string key, content; };
    std::vector<Rec> recs;
    std::unordered_map<std::string, std::string> idToName;   // ownId (#NNNN) -> Class:name key

    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".yaml" && ext != ".yml" && ext != ".txt") continue;
        std::ifstream f(p, std::ios::binary);
        if (!f) continue;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string key, ownId;
        deriveKeyAndId(content, key, ownId);
        if (key.empty()) key = fs::relative(p, root, ec).generic_string();
        if (!ownId.empty()) idToName[ownId] = key;
        recs.push_back({ std::move(key), std::move(content) });
    }

    for (auto& r : recs) {
        std::string body = r.content;
        if (body.rfind("id:", 0) == 0) {   // drop the volatile top-level #NNNN id line
            const std::size_t nl = body.find('\n');
            body = (nl == std::string::npos) ? std::string{} : body.substr(nl + 1);
        }
        if (rosters) body = ResolveIndices(body, *rosters);
        body = resolveNodeRefs(body, idToName);
        rs[r.key] = std::move(body);
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

// tagfile-XML adapter: a Havok tagfile .xml -> RecordSet, keyed IDENTICALLY to the .hkx path
// (id: Class:name). Both sides go XML -> ParseTagfile -> SchemaObject graph -> EmitHky, so the
// same-emitter invariant still holds (a formatting-only diff is impossible). Behaviors are the tested
// surface; a character/project tagfile yields whatever EmitHky projects (may be partial — noted).
bool XmlTagfileRecordSet(const std::string& xmlPath, const havok::schema::SchemaRegistry& reg,
                         RecordSet& rs, std::string& err) {
    std::ifstream f(xmlPath, std::ios::binary);
    if (!f) { err = "cannot open " + xmlPath; return false; }
    std::string xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    havok::model::ParsedTagfile parsed;
    if (!havok::model::ParseTagfile(xml, reg, parsed, err)) return false;
    parsed.identity.refsAsNames = true;   // DIFF-ONLY: emit node refs as Class:name (renumber-immune)
    static std::atomic<unsigned> seq{0};
    const fs::path tmp = fs::temp_directory_path() / ("cb_treediff_xml_" + std::to_string(seq++));
    std::error_code ec; fs::remove_all(tmp, ec);
    if (!havok::model::EmitHky(parsed.identity, reg, tmp.string(), err)) { fs::remove_all(tmp, ec); return false; }
    std::string sErr;
    havok::model::EmitFullBaseScaffolding(parsed.identity, tmp.string(), sErr);   // graphdata -> index->name resolution
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
    p.unorderedKeyedArrays["variables"]              = {"name"};
    p.unorderedKeyedArrays["events"]                 = {"name"};
    p.unorderedKeyedArrays["characterPropertyNames"] = {"name"};   // same roster churn — match by name (flags/type per name)
    // Notify-event arrays: match by the fired event; firing order is not significant.
    p.unorderedKeyedArrays["enterNotifyEvents"] = {"event"};
    p.unorderedKeyedArrays["exitNotifyEvents"]  = {"event"};
    // A state machine's states list is addressed by stateId, not list position — order carries no
    // meaning. Elements are stateInfo refs (rendered as Class:name), so compare as a name multiset.
    p.multisetArrays.insert("states");
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

// Prepare the delta output dir. There is NO remove_all here BY DESIGN — a recursive delete on a `-o`
// typo pointing at a live folder is an unacceptable landmine even behind a sentinel. Instead: refuse a
// non-empty folder that isn't a prior tree-diff output, and clear the previous run by removing ONLY the
// exact files it wrote (recorded in .tree-diff-manifest) — individual fs::remove on single files, then
// fs::remove on now-empty dirs (which no-ops on any non-empty dir). Worst case of a mistargeted -o is a
// few files ADDED to a folder, never a tree deleted. Returns false + err on refusal.
bool PrepareOutDir(const fs::path& outDir, std::string& err) {
    std::error_code ec;
    const fs::path sentinel = outDir / ".tree-diff-out";
    const fs::path manifest = outDir / ".tree-diff-manifest";
    if (fs::exists(outDir, ec)) {
        if (!fs::is_directory(outDir, ec)) { err = "output path exists and is not a directory: " + outDir.string(); return false; }
        if (!fs::is_empty(outDir, ec) && !fs::exists(sentinel, ec)) {
            err = "refusing to write into non-empty '" + outDir.string() +
                  "' — it is not a previous tree-diff output (no .tree-diff-out marker). "
                  "Point -o at an empty or dedicated folder.";
            return false;
        }
        // Clear the PREVIOUS run's files only (from the manifest) — never a recursive delete.
        std::vector<std::string> prev;
        if (std::ifstream in{ manifest }) {
            std::string l;
            while (std::getline(in, l)) {
                while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
                if (!l.empty()) prev.push_back(l);
            }
        }
        std::set<std::string> dirs;   // parent dirs to try to prune afterwards (deepest first)
        for (const auto& rel : prev) {
            const fs::path p = outDir / rel;
            fs::remove(p, ec);   // single file only
            for (fs::path d = p.parent_path(); !d.empty() && d != outDir; d = d.parent_path())
                dirs.insert(d.generic_string());
        }
        std::vector<std::string> dv(dirs.begin(), dirs.end());
        std::sort(dv.begin(), dv.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
        for (const auto& d : dv) fs::remove(fs::path(d), ec);   // removes ONLY if now empty; no-op otherwise
    } else {
        fs::create_directories(outDir, ec);
    }
    std::ofstream(sentinel, std::ios::binary) << "tree-diff output directory\n";
    return true;
}

// Record the exact files this run wrote, so the next run can clear precisely (see PrepareOutDir).
void FinalizeOutDir(const fs::path& outDir) {
    std::error_code ec;
    std::string list;
    for (fs::recursive_directory_iterator it(outDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string rel = fs::relative(it->path(), outDir, ec).generic_string();
        if (rel == ".tree-diff-manifest" || rel == ".tree-diff-out") continue;
        list += rel + "\n";
    }
    std::ofstream(outDir / ".tree-diff-manifest", std::ios::binary) << list;
}

// Recursive tree mode: A and B are DIRECTORIES of .hkx. Pair every .hkx by its relative path,
// run the existing per-file record-keyed diff on each pair (same-emitter: both decompiled from
// binary here), aggregate into ONE delta folder + summary, and surface files present on only one
// side (frequently the actual regression). Domain is "behavior" — DecompileToDir auto-routes
// behavior vs character; project/skeleton .hkx that don't decompile that way are reported as errored,
// not fatal. The delta folder is wiped at the start so each run overwrites (no D:\ pollution).
static TreeDiffOutcome RunRecursive(const TreeDiffOptions& opts, const std::string& outDir, const LogFn& log) {
    TreeDiffOutcome out;
    auto say = [&](const std::string& s) { if (log) log(s); };

    auto collect = [](const fs::path& root) {
        std::map<std::string, fs::path> m;   // lower relpath -> abs path
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".hkx" && ext != ".xml") continue;   // compiled binary OR tagfile-XML
            std::string rel = fs::relative(it->path(), root, ec).generic_string();
            std::string low = rel;
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            m[low] = it->path();
        }
        return m;
    };
    const auto A = collect(opts.a);
    const auto B = collect(opts.b);
    say("tree-diff [recursive]: A " + std::to_string(A.size()) + " unit(s), B " + std::to_string(B.size()) + " unit(s)");

    // Schema registry — needed to parse tagfile-XML inputs (ParseTagfile). Loaded once from --schema.
    havok::schema::SchemaRegistry reg;
    bool haveReg = false;
    if (!opts.schema.empty()) {
        std::string rerr;
        haveReg = reg.LoadDir(opts.schema, rerr);
        if (!haveReg) say("tree-diff: schema load FAILED (" + opts.schema + "): " + rerr + " — .xml inputs will be skipped");
        else          say("tree-diff: schema loaded from " + opts.schema);
    }
    auto buildRS = [&](const fs::path& p, RecordSet& rs, std::string& e) -> bool {
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".xml") {
            if (!haveReg) { e = "tagfile .xml needs --schema <Havok dir>"; return false; }
            return XmlTagfileRecordSet(p.string(), reg, rs, e);
        }
        return DecompileRecordSet(p.string(), opts.skeleton, "beh", rs, e, [](const std::string&){});
    };

    std::error_code ec;
    if (std::string perr; !PrepareOutDir(outDir, perr)) { out.error = perr; say("ERROR: " + perr); return out; }

    std::vector<SummaryEntry> entries;
    std::vector<std::string>  fileOnlyA, fileOnlyB, errored;
    bool anyDiff = false;
    int  paired = 0;

    // union of relative paths
    std::set<std::string> keys;
    for (const auto& [k, v] : A) keys.insert(k);
    for (const auto& [k, v] : B) keys.insert(k);

    for (const std::string& k : keys) {
        const auto ia = A.find(k), ib = B.find(k);
        if (ia == A.end()) { fileOnlyB.push_back(ib->second.filename().generic_string() + "  (" + k + ")"); anyDiff = true; continue; }
        if (ib == B.end()) { fileOnlyA.push_back(ia->second.filename().generic_string() + "  (" + k + ")"); anyDiff = true; continue; }
        RecordSet rsA, rsB;
        std::string err;
        if (!buildRS(ia->second, rsA, err)) { errored.push_back(k + " (A: " + err + ")"); continue; }
        if (!buildRS(ib->second, rsB, err)) { errored.push_back(k + " (B: " + err + ")"); continue; }
        const DiffResult result = DiffRecordSets(rsA, rsB, BehaviorPolicy());
        SummaryEntry entry;
        entry.artifact = k;
        entry.domain   = "behavior";
        for (const auto& [rk, rv] : rsA) if (rsB.count(rk)) ++entry.compared;
        WriteArtifactDelta(outDir, "behavior", k, result, entry);
        entries.push_back(entry);
        ++paired;
        if (result.AnyDifference()) anyDiff = true;
    }

    WriteSummary(outDir, entries, anyDiff);

    // File-level presence report (the A-only / B-only .hkx + any that failed to decompile).
    {
        std::string txt = "# file-level presence (recursive tree diff)\n";
        txt += "paired: " + std::to_string(paired) + "\n\nonly_in_A (" + std::to_string(fileOnlyA.size()) + "):\n";
        for (const auto& f : fileOnlyA) txt += "  - " + f + "\n";
        txt += "\nonly_in_B (" + std::to_string(fileOnlyB.size()) + "):\n";
        for (const auto& f : fileOnlyB) txt += "  - " + f + "\n";
        txt += "\nerrored (" + std::to_string(errored.size()) + "):\n";
        for (const auto& f : errored) txt += "  - " + f + "\n";
        std::ofstream(fs::path(outDir) / "_files.yaml", std::ios::binary) << txt;
    }
    FinalizeOutDir(outDir);

    int totalDiffering = 0;
    for (const auto& e : entries) totalDiffering += e.differing;
    out.ok              = true;
    out.anyDifference   = anyDiff;
    out.artifacts       = paired;
    out.differing       = totalDiffering;
    out.onlyA           = static_cast<int>(fileOnlyA.size());
    out.onlyB           = static_cast<int>(fileOnlyB.size());
    out.summaryPath     = (fs::path(outDir) / "summary.yaml").string();
    say("tree-diff [recursive]: paired " + std::to_string(paired) + ", file only-A " +
        std::to_string(fileOnlyA.size()) + ", file only-B " + std::to_string(fileOnlyB.size()) +
        ", errored " + std::to_string(errored.size()) + ", artifacts-with-diffs " +
        std::to_string(std::count_if(entries.begin(), entries.end(), [](const SummaryEntry& e){ return e.differing || e.onlyA || e.onlyB; })) +
        " -> " + outDir + (anyDiff ? " (DIFFERENCES)" : " (identical)"));
    return out;
}

TreeDiffOutcome RunTreeDiff(const TreeDiffOptions& opts, const LogFn& log) {
    TreeDiffOutcome out;
    auto say = [&](const std::string& s) { if (log) log(s); };

    if (opts.a.empty() || opts.b.empty()) {
        out.error = "tree-diff needs two inputs A and B";
        return out;
    }

    // Recursive tree mode when BOTH inputs are directories (walk + pair .hkx by relative path).
    {
        std::error_code da, db;
        if (fs::is_directory(opts.a, da) && fs::is_directory(opts.b, db))
            return RunRecursive(opts, opts.deltaDir.empty() ? "tree_diff_delta" : opts.deltaDir, log);
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
    if (std::string perr; !PrepareOutDir(outDir, perr)) { out.error = perr; say("ERROR: " + perr); return out; }
    SummaryEntry entry;
    entry.compared = 0;
    for (const auto& [k, v] : rsA) if (rsB.count(k)) ++entry.compared;
    WriteArtifactDelta(outDir, domain, ".", result, entry);
    std::vector<SummaryEntry> entries{entry};
    const bool anyDiff = result.AnyDifference();
    WriteSummary(outDir, entries, anyDiff);
    FinalizeOutDir(outDir);

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
