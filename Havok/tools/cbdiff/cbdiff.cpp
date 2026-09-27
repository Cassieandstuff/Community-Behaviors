// cbdiff — semantic diff of CB's compiled output against a PROVEN reference (Pandora, layered over
// vanilla). Pandora is the in-game-proven oracle; any place CB diverges (that isn't a deliberate CB
// feature) is a CB bug.
//
// Modes:
//   cbdiff adsf <ref.txt> <cb.txt>                        animationdatasinglefile.txt — per-clip diff
//   cbdiff asd  <ref.txt> <cb.txt>                        animationsetdatasinglefile.txt — per-set diff
//   cbdiff asdcrc <ref.txt> <cb.txt> <proj> <set>         dump a set's CB-only CRCs
//   cbdiff treef <refHkx> <cbHkx> <schemaDir>             ONE behavior graph, recursive tree diff
//   cbdiff tree <cbCacheDir> <pandoraMeshes> <vanillaMeshes> <schemaDir>
//                                                         every character behavior; ref = pandora||vanilla
//
// Behavior tree diff (the correct paradigm):
//   • Nodes are NEVER matched by tagfile id — CB reorders ids INTENTIONALLY (that's the point). Each node
//     gets a STRUCTURAL KEY: its path through the reference (call) graph from the root generator, each hop
//     "class:name". Parent context disambiguates identically-named siblings (…/MovementSM:Idle vs
//     …/AttackSM:Idle). The key is identical on both sides whenever the structure matches, so id renumber
//     is invisible; a real structural change shows as an only-in / changed node.
//   • Every field is compared recursively. A pointer field's value (an id) is resolved to the TARGET
//     node's structural key before comparison, so a ref diff means "points to a different node", not a
//     raw-id difference. Non-pointer scalars (stateId, times, flags, animationName) compare verbatim.

#include <interface/AnimationData.h>              // havok::animdata::ParseSingleFile
#include <codec/format/AnimationSetData.h>        // havok::animsetdata::ParseSingleFile
#include <decompile/UnitDecompile.h>              // havok::decompile::DecompileUnit
#include <interface/reflection/HavokSchema.h>     // havok::schema::SetSharedSchemaDir (arm the decompiler)

#include <RymlInclude.h>                          // the sanctioned rapidyaml include (C++20+ shim)

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ryml = c4::yml;
namespace fs   = std::filesystem;

namespace {

int g_diffs = 0;

std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cbdiff: cannot open '%s'\n", p.c_str()); return {}; }
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
std::vector<std::uint8_t> readBytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c)); return s; }
std::string join(const std::vector<std::string>& v, const char* sep = " | ") {
    std::string o; for (std::size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += v[i]; } return o;
}
std::string sv(c4::csubstr s) { return std::string(s.str, s.len); }

// Top-level `key: value` scalar (value unquoted) — quick extraction without a full parse.
std::string field(const std::string& text, const std::string& key) {
    std::istringstream in(text); std::string line; const std::string pfx = key + ":";
    while (std::getline(in, line)) {
        if (line.rfind(pfx, 0) != 0) continue;
        std::string v = line.substr(pfx.size());
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
        if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
        return v;
    }
    return {};
}

// ── animationdatasinglefile.txt ──────────────────────────────────────────────────────────────────
int diffAdsf(const std::string& refPath, const std::string& cbPath) {
    const auto refSF = havok::animdata::ParseSingleFile(readFile(refPath));
    const auto cbSF  = havok::animdata::ParseSingleFile(readFile(cbPath));
    std::printf("== adsf: %zu ref project(s) vs %zu cb project(s) ==\n", refSF.projects.size(), cbSF.projects.size());
    std::unordered_map<std::string, const havok::animdata::Project*> cbByName;
    for (const auto& p : cbSF.projects) cbByName[lower(p.name)] = &p;
    for (const auto& rp : refSF.projects) {
        auto it = cbByName.find(lower(rp.name));
        if (it == cbByName.end()) { std::printf("PROJECT only-in-ref: %s\n", rp.name.c_str()); ++g_diffs; continue; }
        const auto& cp = *it->second;
        std::unordered_map<std::string, const havok::animdata::ClipGenerator*> cbClips, refClips;
        for (const auto& c : cp.clips) cbClips[c.name] = &c;
        for (const auto& c : rp.clips) refClips[c.name] = &c;
        int pd = 0; auto note = [&](const std::string& s){ if(!pd) std::printf("-- %s --\n", rp.name.c_str()); ++pd; ++g_diffs; std::printf("   %s\n", s.c_str()); };
        for (const auto& rc : rp.clips) {
            auto ci = cbClips.find(rc.name);
            if (ci == cbClips.end()) { note("clip only-in-ref: '" + rc.name + "' (animIdx " + rc.animIndex + ")"); continue; }
            const auto& cc = *ci->second;
            if (rc.animIndex != cc.animIndex) note("clip '" + rc.name + "' animIndex: ref " + rc.animIndex + " cb " + cc.animIndex);
            if (rc.triggers != cc.triggers) note("clip '" + rc.name + "' TRIGGERS: ref [" + join(rc.triggers) + "] cb [" + join(cc.triggers) + "]");
        }
        for (const auto& cc : cp.clips) if (!refClips.count(cc.name)) note("clip only-in-cb:  '" + cc.name + "' (animIdx " + cc.animIndex + ")");
    }
    return g_diffs;
}

// ── animationsetdatasinglefile.txt ───────────────────────────────────────────────────────────────
int diffAsd(const std::string& refPath, const std::string& cbPath) {
    const auto refSF = havok::animsetdata::ParseSingleFile(readFile(refPath));
    const auto cbSF  = havok::animsetdata::ParseSingleFile(readFile(cbPath));
    std::printf("== asd: %zu ref project(s) vs %zu cb project(s) ==\n", refSF.projects.size(), cbSF.projects.size());
    std::unordered_map<std::string, const havok::animsetdata::Project*> cbByHdr;
    for (const auto& p : cbSF.projects) cbByHdr[lower(p.header)] = &p;
    for (const auto& rp : refSF.projects) {
        auto it = cbByHdr.find(lower(rp.header));
        if (it == cbByHdr.end()) { std::printf("PROJECT only-in-ref: %s\n", rp.header.c_str()); ++g_diffs; continue; }
        const auto& cp = *it->second;
        std::unordered_map<std::string, const havok::animsetdata::SetFile*> cbSets;
        for (const auto& s : cp.sets) cbSets[lower(s.name)] = &s;
        int pd = 0; auto note = [&](const std::string& s){ if(!pd) std::printf("-- %s --\n", rp.header.c_str()); ++pd; ++g_diffs; std::printf("   %s\n", s.c_str()); };
        for (const auto& rs : rp.sets) {
            auto si = cbSets.find(lower(rs.name));
            if (si == cbSets.end()) { note("set only-in-ref: '" + rs.name + "'"); continue; }
            const auto& cs = *si->second;
            if (rs.equipEvents != cs.equipEvents) note("set '" + rs.name + "' equipEvents: ref [" + join(rs.equipEvents) + "] cb [" + join(cs.equipEvents) + "]");
            if (rs.crcs.size() != cs.crcs.size()) note("set '" + rs.name + "' crc COUNT: ref " + std::to_string(rs.crcs.size()) + " cb " + std::to_string(cs.crcs.size()));
            std::unordered_map<std::string, const havok::animsetdata::Attack*> cbAtk;
            for (const auto& a : cs.attacks) cbAtk[a.event] = &a;
            for (const auto& ra : rs.attacks) {
                auto ai = cbAtk.find(ra.event);
                if (ai == cbAtk.end()) { note("set '" + rs.name + "' attack only-in-ref: '" + ra.event + "' -> [" + join(ra.clips) + "]"); continue; }
                if (ra.clips != ai->second->clips) note("set '" + rs.name + "' attack '" + ra.event + "' clips: ref [" + join(ra.clips) + "] cb [" + join(ai->second->clips) + "]");
            }
        }
    }
    return g_diffs;
}

int dumpAsdCrc(const std::string& refPath, const std::string& cbPath, const std::string& proj, const std::string& set) {
    const auto refSF = havok::animsetdata::ParseSingleFile(readFile(refPath));
    const auto cbSF  = havok::animsetdata::ParseSingleFile(readFile(cbPath));
    auto findSet = [&](const havok::animsetdata::SingleFile& sf) -> const havok::animsetdata::SetFile* {
        for (const auto& p : sf.projects) if (lower(p.header).find(lower(proj)) != std::string::npos)
            for (const auto& s : p.sets) if (lower(s.name).find(lower(set)) != std::string::npos) return &s;
        return nullptr; };
    const auto* rs = findSet(refSF); const auto* cs = findSet(cbSF);
    if (!rs || !cs) { std::printf("set not found\n"); return 1; }
    auto key = [](const havok::animsetdata::CrcTriple& t){ return std::to_string(t.folder)+"/"+std::to_string(t.file)+"/"+std::to_string(t.ext); };
    std::printf("== %s / %s : ref %zu / cb %zu crcs ==\n", proj.c_str(), set.c_str(), rs->crcs.size(), cs->crcs.size());
    std::unordered_map<std::string,int> refHave; for (const auto& t : rs->crcs) refHave[key(t)]++;
    for (std::size_t i = 0; i < cs->crcs.size(); ++i) { const auto k = key(cs->crcs[i]); if (!refHave.count(k)) std::printf("   CB-only [%zu] %s\n", i, k.c_str()); }
    return 0;
}

// ── behavior graph: recursive, structural-key tree diff ───────────────────────────────────────────
// Pointer fields (a scalar/seq value is a node id) — the compiler's FieldRef("X").obj/.objs set. A scalar
// under one of these whose value is a known id is resolved to the target's structural key.
const std::set<std::string> kPtrFields = {
    "EventToCrossBlend","EventToFreezeBlendValue","boneWeights","bones","characterControllerInfo",
    "characterPropertyValues","condition","controlData","data","enterNotifyEvents","event","eventRanges",
    "eventToCheckFor","eventToSend","eventToSendWhenStateOrTransitionChanges","exitNotifyEvents","expressions",
    "footIkDriverInfo","gains","generator","keyFrameHierarchyControlData","keyframedBonesList",
    "mirroredSkeletonInfo","modifier","pBlenderGenerator","pClipGenerator","pDefaultGenerator","pGenerator",
    "pOffsetClipGenerator","pStateMachine","payload","role","rootGenerator","spBoneWeight","stringData",
    "transition","ungroundedEvent","variableBindingSet","variableInitialValues","variant","wildcardTransitions",
    "worldFromModelModeData","ChildrenA","bindings","characterPropertyInfos","children","eventData","eventInfos",
    "events","expressionsData","generators","keyframeInfo","legs","modifiers","namedVariants","stateData",
    "states","variableInfos","variantVariableValues","wordVariableValues",
};

struct BNode { std::string cls, name, text; };

struct BGraph {
    std::unordered_map<std::string, BNode> byId;    // id -> node
    std::unordered_map<std::string, std::string> key; // id -> structural key
    std::string rootId;
};

// Ordered child edges of a node (fieldName, childId), discovered by walking the node's YAML: any scalar
// under a kPtrFields key whose value is a known id is an edge. Recurses into inline maps/seqs (so a
// `transition:` id nested inside the inline `transitions:` list is found too). Order = document order,
// which is identical on both sides for the same structure -> deterministic keys.
void collectEdges(ryml::ConstNodeRef n, const std::string& parentField,
                  const std::unordered_map<std::string, BNode>& ids,
                  std::vector<std::pair<std::string,std::string>>& out) {
    if (n.is_keyval() || (n.has_val() && !n.is_map() && !n.is_seq())) {
        const std::string v = n.has_val() ? sv(n.val()) : std::string();
        if (!v.empty() && kPtrFields.count(parentField) && ids.count(v)) out.emplace_back(parentField, v);
        return;
    }
    if (n.is_map()) for (ryml::ConstNodeRef c : n.children()) collectEdges(c, c.has_key() ? sv(c.key()) : parentField, ids, out);
    else if (n.is_seq()) for (ryml::ConstNodeRef c : n.children()) collectEdges(c, parentField, ids, out); // seq inherits its key's field
}

bool loadGraph(const std::string& hkx, const std::string& outDir, BGraph& g, std::string& err) {
    const auto bytes = readBytes(hkx);
    if (bytes.empty()) { err = "empty/unreadable"; return false; }
    std::error_code ec; fs::remove_all(outDir, ec); fs::create_directories(outDir, ec);
    if (!havok::decompile::DecompileUnit(bytes, outDir).ok) { err = "decompile failed (schema armed?)"; return false; }
    // behavior.yaml -> rootGenerator id (nested/indented under `behavior:`, so match anywhere on the line)
    if (auto t = readFile((fs::path(outDir) / "behavior.yaml").string()); !t.empty()) {
        std::istringstream in(t); std::string line;
        while (std::getline(in, line)) {
            const auto p = line.find("rootGenerator:");
            if (p == std::string::npos) continue;
            std::string v = line.substr(p + std::string("rootGenerator:").size());
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
            while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
            g.rootId = v; break;
        }
    }
    for (auto it = fs::recursive_directory_iterator(outDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".yaml") continue;
        if (it->path().filename() == "behavior.yaml") continue;
        std::string text = readFile(it->path().string());
        const std::string id = field(text, "id"); if (id.empty()) continue;
        g.byId[id] = BNode{ field(text, "class"), field(text, "name"), std::move(text) };
    }
    // Structural keys: DFS from root along ordered child edges. key = parentKey/class:name (+#i on sib dup).
    std::function<void(const std::string&, const std::string&)> dfs =
        [&](const std::string& id, const std::string& parentKey) {
            auto ni = g.byId.find(id); if (ni == g.byId.end() || g.key.count(id)) return;
            const BNode& nd = ni->second;
            std::string base = parentKey + "/" + nd.cls + ":" + nd.name;
            std::string k = base; int dup = 1;
            // de-collide against siblings already keyed to the same base
            for (const auto& [oid, ok] : g.key) if (ok == k) { k = base + "#" + std::to_string(++dup); }
            g.key[id] = k;
            ryml::Tree tr = ryml::parse_in_arena(c4::to_csubstr(nd.text));
            std::vector<std::pair<std::string,std::string>> edges;
            collectEdges(tr.crootref(), "", g.byId, edges);
            for (auto& [fld, cid] : edges) dfs(cid, k);
        };
    if (!g.rootId.empty()) dfs(g.rootId, "");
    // orphans (unreachable from root) still get a key so they're comparable
    for (auto& [id, nd] : g.byId) if (!g.key.count(id)) g.key[id] = "/orphan/" + nd.cls + ":" + nd.name;
    return true;
}

// Recursive value compare of two decompiled node YAML trees, resolving pointer-field scalars to keys.
void cmpVal(ryml::ConstNodeRef a, ryml::ConstNodeRef b, const std::string& fieldName, const std::string& path,
            const BGraph& ga, const BGraph& gb, std::vector<std::string>& out) {
    const bool am = a.is_map(), bm = b.is_map(), as = a.is_seq(), bs = b.is_seq();
    if (am && bm) {
        std::set<std::string> keys;
        for (ryml::ConstNodeRef c : a.children()) if (c.has_key()) keys.insert(sv(c.key()));
        for (ryml::ConstNodeRef c : b.children()) if (c.has_key()) keys.insert(sv(c.key()));
        for (const auto& k : keys) {
            if (k == "id") continue;                                 // id is abstracted away
            ryml::ConstNodeRef ca = a.find_child(c4::to_csubstr(k));
            ryml::ConstNodeRef cb = b.find_child(c4::to_csubstr(k));
            if (ca.invalid()) { out.push_back(path + "." + k + ": only-in-cb"); ++g_diffs; continue; }
            if (cb.invalid()) { out.push_back(path + "." + k + ": only-in-ref"); ++g_diffs; continue; }
            cmpVal(ca, cb, k, path + "." + k, ga, gb, out);
        }
        return;
    }
    if (as && bs) {
        const std::size_t na = a.num_children(), nb = b.num_children();
        if (na != nb) { out.push_back(path + ": seq len ref " + std::to_string(na) + " cb " + std::to_string(nb)); ++g_diffs; }
        for (std::size_t i = 0; i < std::min(na, nb); ++i)
            cmpVal(a[i], b[i], fieldName, path + "[" + std::to_string(i) + "]", ga, gb, out);
        return;
    }
    if (am != bm || as != bs) { out.push_back(path + ": shape differs"); ++g_diffs; return; }
    // both scalar
    std::string va = a.has_val() ? sv(a.val()) : std::string();
    std::string vb = b.has_val() ? sv(b.val()) : std::string();
    if (kPtrFields.count(fieldName)) {                               // resolve id -> structural key
        if (auto i = ga.key.find(va); i != ga.key.end()) va = i->second; else if (ga.byId.count(va)) va = "?" ;
        if (auto i = gb.key.find(vb); i != gb.key.end()) vb = i->second; else if (gb.byId.count(vb)) vb = "?";
    }
    if (va != vb) { out.push_back(path + ": ref '" + va + "' | cb '" + vb + "'"); ++g_diffs; }
}

int diffTreeFiles(const std::string& refHkx, const std::string& cbHkx, const std::string& schemaDir, const std::string& label) {
    if (!schemaDir.empty()) havok::schema::SetSharedSchemaDir(schemaDir);
    const std::string tmp = (fs::temp_directory_path() / "cbdiff").string();
    BGraph rg, cg; std::string e1, e2;
    if (!loadGraph(refHkx, tmp + "_ref", rg, e1)) { std::fprintf(stderr, "%s ref: %s\n", label.c_str(), e1.c_str()); return 1; }
    if (!loadGraph(cbHkx,  tmp + "_cb",  cg, e2)) { std::fprintf(stderr, "%s cb:  %s\n", label.c_str(), e2.c_str()); return 1; }
    // index both by structural key
    std::unordered_map<std::string, std::string> refByKey, cbByKey;   // key -> id
    for (auto& [id, k] : rg.key) refByKey[k] = id;
    for (auto& [id, k] : cg.key) cbByKey[k] = id;
    std::set<std::string> allKeys;
    for (auto& [k, id] : refByKey) allKeys.insert(k);
    for (auto& [k, id] : cbByKey) allKeys.insert(k);
    std::printf("== %s : %zu ref nodes / %zu cb nodes ==\n", label.c_str(), rg.byId.size(), cg.byId.size());
    for (const auto& k : allKeys) {
        auto ri = refByKey.find(k), ci = cbByKey.find(k);
        if (ri == refByKey.end()) { std::printf("NODE only-in-cb : %s\n", k.c_str()); ++g_diffs; continue; }
        if (ci == cbByKey.end()) { std::printf("NODE only-in-ref: %s\n", k.c_str()); ++g_diffs; continue; }
        ryml::Tree ta = ryml::parse_in_arena(c4::to_csubstr(rg.byId[ri->second].text));
        ryml::Tree tb = ryml::parse_in_arena(c4::to_csubstr(cg.byId[ci->second].text));
        std::vector<std::string> out;
        cmpVal(ta.crootref(), tb.crootref(), "", "", rg, cg, out);
        for (const auto& line : out) std::printf("  [%s]%s\n", k.c_str(), line.c_str());
    }
    return 0;
}

// Batch: every character behavior under the CB cache; ref = pandora's file if present, else vanilla's.
int diffTree(const std::string& cbDir, const std::string& panDir, const std::string& vanDir, const std::string& schemaDir) {
    std::error_code ec;
    for (const char* sub : { "actors/character/behaviors", "actors/character/_1stperson/behaviors" }) {
        const fs::path base = fs::path(cbDir) / sub;
        if (!fs::is_directory(base, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(base, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file() || lower(it->path().extension().string()) != ".hkx") continue;
            const std::string rel = fs::relative(it->path(), cbDir, ec).generic_string();
            // meshes-relative: pandora/vanilla store under "meshes/<rel>"
            fs::path pan = fs::path(panDir) / rel, van = fs::path(vanDir) / rel;
            fs::path ref = fs::is_regular_file(pan, ec) ? pan : (fs::is_regular_file(van, ec) ? van : fs::path());
            if (ref.empty()) { std::printf("== %s : no ref (pandora/vanilla) ==\n", rel.c_str()); continue; }
            diffTreeFiles(ref.string(), it->path().string(), schemaDir, rel + (ref == pan ? " [pandora]" : " [vanilla]"));
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage:\n"
                    "  cbdiff adsf   <ref.txt> <cb.txt>\n"
                    "  cbdiff asd    <ref.txt> <cb.txt>\n"
                    "  cbdiff asdcrc <ref.txt> <cb.txt> <proj> <set>\n"
                    "  cbdiff treef  <refHkx> <cbHkx> <schemaDir>\n"
                    "  cbdiff tree   <cbCacheDir> <pandoraMeshes> <vanillaMeshes> <schemaDir>\n");
        return 2;
    }
    const std::string mode = argv[1];
    try {
        if      (mode == "adsf"   && argc >= 4) diffAdsf(argv[2], argv[3]);
        else if (mode == "asd"    && argc >= 4) diffAsd(argv[2], argv[3]);
        else if (mode == "asdcrc" && argc >= 6) return dumpAsdCrc(argv[2], argv[3], argv[4], argv[5]);
        else if (mode == "treef"  && argc >= 5) diffTreeFiles(argv[2], argv[3], argv[4], fs::path(argv[3]).filename().string());
        else if (mode == "tree"   && argc >= 6) diffTree(argv[2], argv[3], argv[4], argv[5]);
        else { std::printf("cbdiff: bad mode/args\n"); return 2; }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cbdiff: %s\n", e.what());
        return 1;
    }
    std::printf("\n== %d divergence(s) ==\n", g_diffs);
    return g_diffs ? 1 : 0;
}
