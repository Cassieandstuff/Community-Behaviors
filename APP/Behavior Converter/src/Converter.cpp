#include "Converter.h"

#include <havok/sct/PatchConverter.h>
#include <havok/sct/CharacterDecompiler.h>   // DecompileToDir (character units)
#include <havok/sct/DeltaDeriver.h>          // DeriveLooseBehaviorDelta (loose full graphs)
#include <havok/sct/BehaviorCompiler.h>      // CompileBehavior (base unit -> vanilla binary)
#include <havok/sct/HavokFile.h>             // ReadHavokFile / WriteHavokFile
#include <havok/skeleton/SkeletonImport.h>   // LoadSkeletonsFromHkx / ReadSkeletonPhysics (havok-skeleton)
#include <havok/skeleton/SkeletonYaml.h>     // EmitSkeletonYamlTree — SkeletonData -> bonelist.yaml + bones/ unit
#include <havok/model/yaml/HkyArchive.h>     // Skyrim.hky base = the loose-derive vanilla source
#include <havok/model/yaml/YamlBehaviorLoader.h>  // LoadMerged (base unit -> BehaviorData)
#include <havok/sct/AnimDataFromBehavior.h>        // DeriveClipInputsFromBehavior / DeriveProjectClipList
#include <havok/anim/AnimationSetData.h>     // parse vanilla animationsetdatasinglefile
#include <havok/anim/AnimSetDataYaml.h>      // EmitMovesetsYaml (vanilla decompose)
#include <havok/anim/AnimationData.h>        // parse vanilla animationdatasinglefile
#include <havok/anim/AnimDataYaml.h>         // EmitMotionYaml (motion decompose)
#include <havok/core/PackFileDeserializer.h> // object-count gate for template matching + ConstructAllOfClass (pass 2d)
#include <havok/core/BinaryReaderEx.h>       // BinaryReaderEx — drives ConstructAllOfClass
#include <havok/anim/AnimationCompiler.h>    // havok::anim::CompileAnimation — recompile leg (schema-native)
#include <havok/anim/AnimationDecompiler.h>  // havok::anim::DecompileAnimation — schema-native import leg
#include <havok/anim/AnimationYamlLoader.h>  // AnimationYamlLoader::Load — animation.yaml -> AnimationDef
#include <havok/classes/Generators.h>        // hkbClipGenerator / hkbBehaviorReferenceGenerator (pass 2d RBG walk)
#include <havok/sct/TagfileOracle.h>         // AlignTagfile — base-source fidelity gate (pass 2a)
#include <havok-model/HavokModel.h>          // ConvertModDelta — the DEFAULT data-driven per-mod delta
#include <havok-schema/HavokSchema.h>        // SchemaRegistry (Havok/ class descriptors)
#include "NemesisSetDataConvert.h"   // CommunityBehaviors::asd::ConvertNemesisSetData (shared with br-nemesis-to-hky)
#include "FnisConverter.h"           // CommunityBehaviors::fnis::ConvertFnis (shared with br-fnis-to-hky)
#include "PatchPlan.h"               // BuildBaseMaps / BuildPlan / DumpPlan — the Pandora normalization prelude (Phase 0)
#include <sct-utilities/SctUtilities.h>       // ZipDir — pack the staged master into one Skyrim.hky

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace bconv {
namespace {

constexpr const char* kBehSub    = "meshes/actors/character/behaviors";
constexpr const char* kBehSub1st = "meshes/actors/character/_1stperson/behaviors";

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Decode XML entity references to their literal characters — mirrors havok::model
// CharacterYamlLoader::xmlUnescape. The character DECOMPILER emits roster paths through an
// XML-style pipeline, so a shared-killmove path arrives as "..\SharedKillMoves\Human&amp;Boar\..".
// The vanilla template roster it is diffed against is LITERAL ('&'), so an escaped line never
// matches — every killmove looks "new" and gets written as a phantom roster addition. Those
// phantom paths don't exist on disk (bind to nothing) yet still pad animationNames, and OAR sets
// its synchronized-clip index offset to animationNames.size() — so the padding underflows OAR's
// (uint16) binding-index subtraction → out-of-bounds → the char-setup rep-stosq CTD. Normalizing
// both sides to literal before the diff keeps the emitted delta to REAL additions only.
std::string UnescapeXml(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;")  == 0) { o += '&';  i += 5; continue; }
            if (s.compare(i, 4, "&lt;")   == 0) { o += '<';  i += 4; continue; }
            if (s.compare(i, 4, "&gt;")   == 0) { o += '>';  i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { o += '"';  i += 6; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { o += '\''; i += 6; continue; }
        }
        o += s[i++];
    }
    return o;
}

// The clip animationNames + RBG behaviorNames read straight out of a loose behavior .hkx —
// the seed for pass 2d's RBG-chain roster derivation (BR-14).
struct LooseBehaviorRefs {
    std::vector<std::string> animationNames;   // every hkbClipGenerator::m_animationName
    std::vector<std::string> behaviorNames;    // every hkbBehaviorReferenceGenerator::m_behaviorName (recurse)
};

// Construct ONLY the clip + RBG objects from a loose behavior .hkx (ConstructAllOfClass, the
// SkeletonImport pattern) — never a full-graph Deserialize, which would walk (and could throw on)
// unrelated classes. Returns false on a read/parse failure (caller skips it, non-fatal).
bool ReadLooseBehaviorRefs(const std::string& hkxPath, LooseBehaviorRefs& out) {
    std::vector<std::uint8_t> bytes;
    std::string               err;
    if (!havok::sct::ReadHavokFile(hkxPath, bytes, &err)) return false;
    try {
        havok::PackFileDeserializer des;
        havok::BinaryReaderEx       br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        des.DeserializePartially(br);
        const bool le = des._header.Endian == 0;
        const bool p8 = des._header.PointerSize == 8;
        {
            havok::BinaryReaderEx dr(le, p8, des.DataSectionBytes());
            for (const auto& o : des.ConstructAllOfClass(dr, "hkbClipGenerator"))
                if (auto c = std::dynamic_pointer_cast<havok::hkbClipGenerator>(o); c && !c->m_animationName.empty())
                    out.animationNames.push_back(c->m_animationName);
        }
        {
            havok::BinaryReaderEx dr(le, p8, des.DataSectionBytes());
            for (const auto& o : des.ConstructAllOfClass(dr, "hkbBehaviorReferenceGenerator"))
                if (auto r = std::dynamic_pointer_cast<havok::hkbBehaviorReferenceGenerator>(o); r && !r->m_behaviorName.empty())
                    out.behaviorNames.push_back(r->m_behaviorName);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Pull the single-quoted value after `key` on a YAML line (e.g. behaviorName: 'Behaviors\X.hkx').
// Empty if the key isn't on the line.
std::string YamlQuoted(const std::string& line, const char* key) {
    const auto p = line.find(key);
    if (p == std::string::npos) return {};
    const auto q1 = line.find('\'', p);
    if (q1 == std::string::npos) return {};
    const auto q2 = line.find('\'', q1 + 1);
    if (q2 == std::string::npos) return {};
    return line.substr(q1 + 1, q2 - q1 - 1);
}

// A graph <g> is available when its tagfile template <g>.xml exists — that XML is the oracle's
// alignment source and is always required. The vanilla BINARY is now sourced from the shipped
// Skyrim.hky base (compiled per graph, gated for fidelity in ConvertLoadOrder); a sibling
// <g>.hkx is OPTIONAL, kept only as the fallback binary for the handful of graphs whose base
// unit doesn't recompile tagfile-clean (0_master, 1hm_behavior). So the scan keys on .xml alone.
std::vector<std::string> ScanGraphs(const fs::path& templatesDir) {
    std::vector<std::string> graphs;
    std::error_code ec;
    if (!fs::is_directory(templatesDir, ec)) return graphs;
    for (auto& e : fs::directory_iterator(templatesDir, ec)) {
        if (ec) break;
        if (!e.is_regular_file() || e.path().extension() != ".xml") continue;
        graphs.push_back(e.path().stem().string());
    }
    std::sort(graphs.begin(), graphs.end());
    return graphs;
}

// Vanilla character templates: <templates>/characters/ mirrors the meshes-relative game
// tree, e.g. characters/actors/character/characters/defaultmale.hkx (and "characters
// female/defaultfemale.hkx" — the space is part of the real path and thus of the serve
// key). Each file becomes a decompiled character unit in Skyrim.hky at
// meshes/<relative path>/, which the Resolver serves via CharacterYamlLoader::LoadMerged
// + CompileCharacter with mod roster deltas overlaid — the compile-time replacement for
// the runtime animationNames injection. Returns meshes-relative paths ("actors/.../x.hkx").
std::vector<std::string> ScanCharacterTemplates(const fs::path& templatesDir) {
    std::vector<std::string> chars;
    std::error_code ec;
    const fs::path root = templatesDir / "characters";
    if (!fs::is_directory(root, ec)) return chars;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".hkx") continue;
        chars.push_back(it->path().lexically_relative(root).generic_string());
    }
    std::sort(chars.begin(), chars.end());
    return chars;
}

// Union `names` into a bundle's character-unit roster delta: <bundle>/<characterServePath>/data/
// animations.yaml — a YAML block sequence of single-quoted, case-preserved animation paths. This is
// the NATIVE roster contract the schema reads (CharacterYamlLoader::LoadMerged -> unionAnimations),
// the replacement for the bundle-root animationnames/<stem>.txt shim: the runtime overlays each
// delta layer's data/animations.yaml onto the base character, keyed by the unit's serve path. Merges
// with what the delta file already holds (deduped case-insensitively). `characterServePath` is a
// meshes-relative serve key ("meshes/actors/horse/characters/horse.hkx"). Returns count newly added.
int AddRosterToCharacterUnit(const fs::path& bundleDir, const std::string& characterServePath,
                             const std::vector<std::string>& names) {
    if (names.empty() || characterServePath.empty()) return 0;
    const fs::path yaml = bundleDir / fs::path(characterServePath) / "data" / "animations.yaml";
    std::error_code we;
    std::vector<std::string>        lines;   // existing (order-preserving)
    std::unordered_set<std::string> have;    // lowercased dedup
    // Parse any existing delta (single-quoted block seq; '' -> literal '), same shape readRoster uses.
    if (std::ifstream in{ yaml }) {
        std::string l;
        while (std::getline(in, l)) {
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
            std::size_t s = l.find_first_not_of(" \t");
            if (s == std::string::npos) continue;
            std::string t = l.substr(s);
            if (t.empty() || t[0] == '#') continue;
            if (t.rfind("- ", 0) == 0) t = t.substr(2);
            else if (t[0] == '-')      t = t.substr(1);
            s = t.find_first_not_of(" \t");
            if (s != std::string::npos) t = t.substr(s);
            if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') {
                const std::string inner = t.substr(1, t.size() - 2); std::string un;
                for (std::size_t i = 0; i < inner.size(); ++i)
                    if (inner[i] == '\'' && i + 1 < inner.size() && inner[i + 1] == '\'') { un += '\''; ++i; }
                    else un += inner[i];
                t = std::move(un);
            }
            if (!t.empty() && have.insert(ToLower(t)).second) lines.push_back(std::move(t));
        }
    }
    int added = 0;
    for (const auto& a : names)
        if (!a.empty() && have.insert(ToLower(a)).second) { lines.push_back(a); ++added; }
    if (added == 0) return 0;
    fs::create_directories(yaml.parent_path(), we);
    std::string text;
    for (const auto& l : lines) {
        std::string q; q.reserve(l.size() + 2);
        q += '\'';
        for (char c : l) { q += c; if (c == '\'') q += '\''; }   // YAML single-quote escaping
        q += '\'';
        text += "- " + q + "\n";
    }
    std::ofstream out(yaml, std::ios::binary);
    out << text;
    if (!out) return 0;   // write failed (e.g. MAX_PATH) — report nothing added, don't claim success
    return added;
}

// Nemesis codes = the child dirs of <dataDir>/Nemesis_Engine/mod/. Under MO2's VFS this
// is the union of every active behavior mod's code dir (bfco, colis, tdmv, ...).
std::vector<std::string> ScanCodes(const fs::path& dataDir) {
    std::vector<std::string> codes;
    std::error_code ec;
    const fs::path modRoot = dataDir / "Nemesis_Engine" / "mod";
    if (!fs::is_directory(modRoot, ec)) return codes;
    for (auto& e : fs::directory_iterator(modRoot, ec)) {
        if (ec) break;
        if (e.is_directory()) codes.push_back(e.path().filename().string());
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

// Pandora's OWN behavior load order, from <dataDir>/Pandora_Engine/ActiveMods.json (written by
// Pandora Behaviour Engine on its last run; under MO2's VFS the active Pandora output shadows it in).
// It is the authoritative order for the merged Pandora.hky — the same priority the user sets in
// Pandora's UI. Format: a flat JSON array of { "code": "bfco", "active": true, "priority": 1 } where
// priority 1 = HIGHEST (the winner). Hand-parsed (the converter stays dependency-lean — no nlohmann).
struct PandoraModEntry { std::string code; bool active = true; int priority = 0; };
std::vector<PandoraModEntry> LoadPandoraOrder(const fs::path& dataDir) {
    std::vector<PandoraModEntry> out;
    std::ifstream f(dataDir / "Pandora_Engine" / "ActiveMods.json", std::ios::binary);
    if (!f) return out;
    const std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto npos = std::string::npos;
    // Split into per-object chunks on '{' … '}' (the entries are flat, no nesting), then pull the
    // three known keys out of each chunk by name so key ORDER inside an object doesn't matter.
    std::size_t pos = 0;
    while (true) {
        const std::size_t ob = s.find('{', pos);
        if (ob == npos) break;
        const std::size_t cb = s.find('}', ob);
        if (cb == npos) break;
        const std::string chunk = s.substr(ob, cb - ob);
        pos = cb + 1;
        auto strVal = [&](const char* key) -> std::string {
            const std::size_t k = chunk.find(std::string("\"") + key + "\"");
            if (k == npos) return {};
            const std::size_t c = chunk.find(':', k);              if (c == npos) return {};
            const std::size_t q1 = chunk.find('"', c);            if (q1 == npos) return {};
            const std::size_t q2 = chunk.find('"', q1 + 1);       if (q2 == npos) return {};
            return chunk.substr(q1 + 1, q2 - q1 - 1);
        };
        auto rawVal = [&](const char* key) -> std::string {
            const std::size_t k = chunk.find(std::string("\"") + key + "\"");
            if (k == npos) return {};
            std::size_t e = chunk.find(':', k);                   if (e == npos) return {};
            ++e;
            while (e < chunk.size() && std::isspace((unsigned char)chunk[e])) ++e;
            const std::size_t st = e;
            while (e < chunk.size() && chunk[e] != ',' && chunk[e] != '\n' && chunk[e] != '\r') ++e;
            return chunk.substr(st, e - st);
        };
        PandoraModEntry pe;
        pe.code = strVal("code");
        if (pe.code.empty()) continue;
        pe.active   = rawVal("active").find("true") != npos;
        const std::string pr = rawVal("priority");
        pe.priority = pr.empty() ? 0 : std::atoi(pr.c_str());
        out.push_back(std::move(pe));
    }
    return out;
}

// Conflict pre-scan (shared by the conversion + the Pandora Order UI so they never drift). Given the
// resolved merge order (WINNER LAST), walk each code's behavior-graph patch dirs and report where order
// matters: per graph, the codes touching it and the base nodes (#NNNN, not #code$N new nodes) that 2+
// codes override — the order-sensitive collisions. `winner` = the collider latest in the merge order.
std::vector<PandoraGraphConflict> ScanGraphConflicts(const fs::path& dataDir,
                                                     const std::vector<std::string>& mergeOrder) {
    std::unordered_map<std::string, int> rank;
    for (int i = 0; i < (int)mergeOrder.size(); ++i) rank[mergeOrder[i]] = i;

    std::map<std::string, std::map<std::string, std::vector<std::string>>> nodeHits;  // graph->node->codes
    std::map<std::string, std::vector<std::string>>                        codesByGraph;

    auto scanGraphDir = [&](const std::string& code, const fs::path& gdir, const std::string& gname) {
        std::error_code se;
        bool touched = false;
        for (fs::directory_iterator it(gdir, se), end; !se && it != end; it.increment(se)) {
            if (!it->is_regular_file()) continue;
            const std::string fn = it->path().filename().string();
            if (fn.size() < 6 || fn[0] != '#' || it->path().extension() != ".txt") continue;
            touched = true;
            const std::string id = fn.substr(1, fn.size() - 5);   // strip '#' and ".txt"
            if (!id.empty() && id.find('$') == std::string::npos &&
                std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c); }))
                nodeHits[gname][id].push_back(code);
        }
        if (touched) codesByGraph[gname].push_back(code);
    };

    for (const auto& code : mergeOrder) {
        const fs::path cd = dataDir / "Nemesis_Engine" / "mod" / code;
        std::error_code se;
        for (fs::directory_iterator it(cd, se), end; !se && it != end; it.increment(se)) {
            if (!it->is_directory()) continue;
            const std::string sub  = it->path().filename().string();
            const std::string subl = ToLower(sub);
            if (subl == "animationsetdatasinglefile" || subl == "animationdatasinglefile") continue;
            if (subl == "_1stperson") {
                for (fs::directory_iterator jt(it->path(), se), jend; !se && jt != jend; jt.increment(se))
                    if (jt->is_directory())
                        scanGraphDir(code, jt->path(), "_1stperson/" + jt->path().filename().string());
            } else {
                scanGraphDir(code, it->path(), sub);
            }
        }
    }

    std::vector<PandoraGraphConflict> out;
    for (const auto& [g, codesV] : codesByGraph) {
        if (codesV.size() < 2) continue;
        PandoraGraphConflict gc; gc.graph = g; gc.codes = codesV;
        int best = -1;
        for (const auto& [node, hits] : nodeHits[g]) {
            if (hits.size() < 2) continue;
            ++gc.nodeCollisions;
            for (const auto& c : hits) { const int r = rank.count(c) ? rank[c] : -1; if (r > best) { best = r; gc.winner = c; } }
        }
        if (gc.winner.empty()) gc.winner = codesV.back();
        out.push_back(std::move(gc));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// MO2-attributed discovery — find, per INSTALLED mod, both loose-behavior legs:
//   (A) its Nemesis code dir(s):     <mod>/Nemesis_Engine/mod/<code>/
//   (B) its precompiled graph(s):    <mod>/meshes/**/behaviors/*.hkx  (a whole compiled graph,
//       e.g. HorsePower's horsebehavior.hkx — carries content NO Nemesis patch expresses)
// so both can be grouped into one <modName>.hky. Without this, ScanCodes sees the VFS-flattened
// union (mod-of-origin lost) and pass 1d derives precompiled graphs anonymously into one
// BehaviorFiles.hky. Discovery is an ENHANCEMENT: if no MO2 instance resolves, the caller keeps
// the un-attributed path unchanged.  (Two-disjoint-layers case: HorsePower ships a precompiled
// horsebehavior.hkx = jump/sprint/walk AND an hpmhr Nemesis patch = mounted attack-followup, with
// DIFFERENT content in each — both legs must convert and merge; converting one drops the other.)
struct LooseModContribution {
    std::string           modName;             // installed mod folder name -> <modName>.hky
    int                   priority = 0;        // modlist rank; SMALLER = higher priority (top = winner)
    std::vector<std::string> nemesisCodes;     // leg A: Nemesis_Engine/mod/<code>
    std::vector<fs::path>    precompiledGraphs; // leg B: absolute paths to loose behavior .hkx
};

// Cheap classnames-peek: is this .hkx a behavior GRAPH (not an animation / other asset)? A
// behavior tagfile/packfile embeds the class name "hkbBehaviorGraph" in its class table; an
// animation embeds "hkaAnimationContainer" and never that. Substring-scan the raw bytes — no
// full deserialize (which could throw on unrelated classes). Guards leg B from eating an OAR
// animation config that happens to sit near a behaviors/ path.
bool PeekIsBehaviorHkx(const fs::path& hkx) {
    std::ifstream f(hkx, std::ios::binary);
    if (!f) return false;
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return buf.find("hkbBehaviorGraph") != std::string::npos;
}

// Is this mod a behavior ENGINE (Pandora / Nemesis), not content? Such a mod ships a
// Nemesis_Engine/mod/ block of the engine's OWN sample/base codes (Pandora: snusnu, tkds) plus
// the engine tool — converting those codes would inject the engine's scaffolding as if it were a
// content delta. Marker: Pandora's Pandora_Engine/ dir, or an engine .exe at the mod root (a
// content mod never ships one). Its codes are excluded from conversion entirely.
bool IsEngineMod(const fs::path& modDir) {
    std::error_code ec;
    if (fs::is_directory(modDir / "Pandora_Engine", ec)) return true;   // Pandora
    for (fs::directory_iterator it(modDir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        if (ToLower(it->path().extension().string()) != ".exe") continue;
        const std::string n = ToLower(it->path().filename().string());
        if (n.find("pandora") != std::string::npos || n.find("nemesis") != std::string::npos) return true;
    }
    return false;
}

// Resolve <instanceRoot>/profiles/<profile>/modlist.txt -> the enabled mods, TOP-FIRST (MO2
// lists highest-priority first). `+`=enabled, `-`=disabled, `#`/`*`=separators/comments. The
// active profile comes from ModOrganizer.ini's selected_profile when present, else the sole
// profile dir, else "Default". Returns {} (ok=false) if the layout isn't an MO2 instance.
struct Mo2Layout {
    bool                     ok = false;
    fs::path                 modsDir;
    std::vector<std::string> enabledTopFirst;
};
Mo2Layout ResolveMo2Layout(const fs::path& instanceRoot) {
    Mo2Layout out;
    std::error_code ec;
    const fs::path mods = instanceRoot / "mods";
    const fs::path profs = instanceRoot / "profiles";
    if (!fs::is_directory(mods, ec) || !fs::is_directory(profs, ec)) return out;

    // Active profile: ModOrganizer.ini selected_profile, else sole profile dir, else "Default".
    std::string profile;
    if (std::ifstream ini{ instanceRoot / "ModOrganizer.ini" }) {
        std::string line;
        while (std::getline(ini, line)) {
            const auto p = line.find("selected_profile");
            if (p == std::string::npos) continue;
            const auto eq = line.find('=', p);
            if (eq == std::string::npos) break;
            std::string v = line.substr(eq + 1);
            // may be wrapped as @ByteArray(<name>) or @Variant(...) — strip a ByteArray wrapper.
            const auto ba = v.find("ByteArray(");
            if (ba != std::string::npos) {
                const auto o = v.find('(', ba), c = v.rfind(')');
                if (o != std::string::npos && c != std::string::npos && c > o) v = v.substr(o + 1, c - o - 1);
            }
            while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
            while (!v.empty() && (v.front() == ' ' || v.front() == '"')) v.erase(v.begin());
            while (!v.empty() && v.back() == '"') v.pop_back();
            if (!v.empty()) profile = v;
            break;
        }
    }
    if (profile.empty()) {
        // sole profile dir, else Default
        std::vector<std::string> dirs;
        for (fs::directory_iterator it(profs, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec)) dirs.push_back(it->path().filename().string());
        profile = (dirs.size() == 1) ? dirs.front() : std::string("Default");
    }
    std::ifstream ml(profs / profile / "modlist.txt");
    if (!ml) return out;
    // modlist.txt is top-first already; keep that order for the enabled entries.
    std::string line;
    while (std::getline(ml, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] != '+') continue;   // '-' disabled, '#'/'*' separators
        out.enabledTopFirst.push_back(line.substr(1));
    }
    out.ok = !out.enabledTopFirst.empty();
    out.modsDir = mods;
    return out;
}

// Walk the enabled mods (in priority order) and collect both loose-behavior legs per mod. A
// precompiled graph is NOT filtered against the vanilla stem set — a vanilla-stem graph (horse)
// is exactly the derive case, and a non-vanilla stem (custom creature) is left for the future
// new-graph path; both are recorded here.
std::vector<LooseModContribution> DiscoverContributions(const Mo2Layout& mo2, const LogFn& log,
                                                        std::unordered_set<std::string>& excludedCodesLower) {
    std::vector<LooseModContribution> out;
    std::error_code ec;
    int rank = 0;
    for (const std::string& modName : mo2.enabledTopFirst) {
        const fs::path modDir = mo2.modsDir / modName;
        if (!fs::is_directory(modDir, ec)) { ++rank; continue; }  // enabled-but-absent (edge case)

        // Behavior ENGINE (Pandora/Nemesis) — not content. Exclude its Nemesis codes from
        // conversion entirely (they'd otherwise convert as anonymous <code>.hky scaffolding).
        if (IsEngineMod(modDir)) {
            const fs::path enem = modDir / "Nemesis_Engine" / "mod";
            int nex = 0;
            if (fs::is_directory(enem, ec))
                for (fs::directory_iterator it(enem, ec), end; !ec && it != end; it.increment(ec))
                    if (it->is_directory(ec)) { excludedCodesLower.insert(ToLower(it->path().filename().string())); ++nex; }
            if (log)
                log("  engine mod '" + modName + "' skipped (behavior engine, not content) — " +
                    std::to_string(nex) + " engine code(s) excluded.");
            ++rank;
            continue;
        }

        LooseModContribution c;
        c.modName  = modName;
        c.priority = rank++;

        // Leg A — Nemesis codes owned by this mod.
        const fs::path nem = modDir / "Nemesis_Engine" / "mod";
        if (fs::is_directory(nem, ec))
            for (fs::directory_iterator it(nem, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_directory(ec)) c.nemesisCodes.push_back(it->path().filename().string());

        // Leg B — precompiled loose behavior graphs (…/behaviors/*.hkx that are behavior graphs).
        const fs::path meshes = modDir / "meshes";
        if (fs::is_directory(meshes, ec)) {
            for (fs::recursive_directory_iterator it(meshes, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code fe;
                if (!it->is_regular_file(fe)) continue;
                const fs::path& p = it->path();
                if (ToLower(p.extension().string()) != ".hkx") continue;
                // must sit under a `behaviors/` segment (animations live under animations/) — OR a
                // `behaviors <x>` space-folder (canine ships `behaviors wolf`, `behaviors dog`, …);
                // an exact "behaviors" match misses those, leaving the loose graph UN-attributed so it
                // falls to the anonymous BehaviorFiles.hky instead of its owning mod's bundle.
                bool underBehaviors = false;
                for (const auto& seg : p) {
                    const std::string s = ToLower(seg.string());
                    if (s == "behaviors" || s.rfind("behaviors ", 0) == 0) { underBehaviors = true; break; }
                }
                if (!underBehaviors) continue;
                if (!PeekIsBehaviorHkx(p)) continue;   // skip anims / non-graph assets
                c.precompiledGraphs.push_back(p);
            }
        }

        if (!c.nemesisCodes.empty() || !c.precompiledGraphs.empty()) {
            if (log)
                log("  mod '" + modName + "': " + std::to_string(c.nemesisCodes.size()) +
                    " Nemesis code(s), " + std::to_string(c.precompiledGraphs.size()) +
                    " precompiled graph(s).");
            out.push_back(std::move(c));
        }
    }
    return out;
}

// Best-effort auto-derive of the MO2 instance root when the caller gave none. dataDir is usually
// the VFS-merged game Data (NOT under the instance), so this rarely succeeds — the explicit
// Options::mo2Instance is the reliable path — but try the cheap signals: climb dataDir's parents
// for a mods/+profiles/ pair, then scan %LOCALAPPDATA%\ModOrganizer\*\ for an instance ini.
fs::path DeriveInstanceRoot(const fs::path& dataDir, const std::string& explicitRoot, const LogFn& log) {
    std::error_code ec;
    if (!explicitRoot.empty()) {
        const fs::path r = explicitRoot;
        if (fs::is_directory(r / "mods", ec) && fs::is_directory(r / "profiles", ec)) return r;
        if (log) log("  MO2 instance override '" + explicitRoot + "' is not an instance (no mods/+profiles/) — ignoring.");
        return {};
    }
    // climb dataDir parents
    for (fs::path p = dataDir; !p.empty() && p != p.root_path(); p = p.parent_path())
        if (fs::is_directory(p / "mods", ec) && fs::is_directory(p / "profiles", ec)) return p;
    // %LOCALAPPDATA%\ModOrganizer\<inst>\ (non-portable instances keep mods/ elsewhere via ini —
    // only report a hit when the folder itself carries mods/+profiles/, i.e. a self-contained one).
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4996)   // getenv is fine here — read-only, single-threaded startup
#endif
    const char* la = std::getenv("LOCALAPPDATA");
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
    if (la) {
        const fs::path moRoot = fs::path(la) / "ModOrganizer";
        if (fs::is_directory(moRoot, ec))
            for (fs::directory_iterator it(moRoot, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_directory(ec) &&
                    fs::is_directory(it->path() / "mods", ec) && fs::is_directory(it->path() / "profiles", ec))
                    return it->path();
    }
    return {};
}

// Verbatim-copy a mod's Nemesis anim-data patch dirs into the bundle. Source:
// Derive a mod bundle's animationdata as per-clip yaml DELTAS — the BR-native replacement for the
// verbatim Nemesis copy. Two sources join by clip NAME:
//   • the mod's <codeDir>/animationdatasinglefile/<Project>~<n>/ is the AUTHORITATIVE addition set:
//     which clips the mod adds to which PROJECT, and their MOTION (proprietary root-motion extraction
//     that is NOT derivable from the graph). This is exactly what the old verbatim copy shipped — and
//     it is the correct SET (a mod behaviour delta's clips/ carries the mod's WHOLE clip set, vanilla
//     records included, so it over-represents; the Nemesis animationdata is precisely the additions).
//   • the bundle's behaviour clip generators carry each clip's animationName (the Nemesis clip record
//     does not). We read them straight from each behaviour unit's clips/ dir — a mod behaviour DELTA
//     has no behavior.yaml, so a whole-graph load fails; ReadClipInputsFromClipsDir reads the clip
//     nodes directly — and join clip-name -> animationName.
// For each Nemesis addition we emit clips/<projStem>/<clip>.yaml keyed by `animation:` (+ its crop/
// speed/triggers verbatim from Nemesis) and, when it carries motion, motion/<projStem>/<clip>.yaml (the
// real record). NO animIndex is written: the runtime resolves it by name against the MERGED roster
// (base + this mod's animationnames additions), so a mod clip lands on the exact roster slot char-setup
// binds — replacing the old high-band allocation that could not line up with the roster. Returns the
// number of clip records emitted.
int DeriveModAnimDeltas(const fs::path& codeDir, const fs::path& bundle,
                        const fs::path& /*dataDir*/, const LogFn& say) {
    std::error_code ec;
    const fs::path nemRoot = codeDir / "animationdatasinglefile";
    if (!fs::is_directory(nemRoot, ec)) return 0;   // this mod ships no animationdata

    // ── behaviour clip-name -> animationName (join key) ──
    // Every behaviour unit in the bundle ("*.hkx" dir with a clips/ subdir). First occurrence wins;
    // a clip name maps to one animation (a Nemesis addition names an animation exactly once).
    std::unordered_map<std::string, std::string> animOf;
    for (fs::recursive_directory_iterator it(bundle / "meshes",
             fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code de;
        if (!it->is_directory(de)) continue;
        const fs::path unit = it->path();
        if (ToLower(unit.extension().string()) != ".hkx") continue;
        const fs::path clipsDir = unit / "clips";
        if (!fs::is_directory(clipsDir, de)) continue;
        for (const auto& dc : havok::sct::ReadClipInputsFromClipsDir(clipsDir.string()))
            if (!dc.name.empty() && !dc.animationName.empty())
                animOf.emplace(dc.name, dc.animationName);
    }
    // animOf may be empty (a pure-animationdata mod with no behaviour clips) — still emit every Nemesis
    // record; the animationName is only browsable metadata now (the runtime assigns the high-band index).

    const fs::path animRoot = bundle / "meshes" / "animationdatasinglefile.txt";
    int written = 0, unmatched = 0;

    // ── per Nemesis project dir (DefaultMale~1, DefaultFemale~1, …) ──
    for (fs::directory_iterator pi(nemRoot, ec), pend; !ec && pi != pend; pi.increment(ec)) {
        std::error_code de;
        if (!pi->is_directory(de)) continue;
        const std::string dirName = pi->path().filename().string();          // "DefaultMale~1"
        const std::string projName = dirName.substr(0, dirName.find('~'));   // "DefaultMale"
        const std::string projStem = ToLower(projName);                      // clips/<stem>/ key (== master's)

        std::vector<std::pair<std::string, std::string>> files;
        for (fs::directory_iterator fi(pi->path(), de), fend; !de && fi != fend; fi.increment(de)) {
            std::error_code fe;
            if (!fi->is_regular_file(fe) || fi->path().extension() != ".txt") continue;
            std::ifstream f(fi->path(), std::ios::binary);
            std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            files.emplace_back(fi->path().filename().string(), std::move(body));
        }
        if (files.empty()) continue;

        havok::animdata::ProjectPatch pp;
        try { pp = havok::animdata::AssembleProjectPatch(dirName, files); }
        catch (const std::exception& e) { say("  bad animationdata dir '" + dirName + "': " + e.what()); continue; }

        const fs::path cdir = animRoot / "clips" / projStem;
        const fs::path mdir = animRoot / "motion" / projStem;
        std::set<std::string> used;   // filename dedup within this project (case-insensitive)

        for (const auto& add : pp.additions) {
            if (!add.hasClip) continue;                       // a motion-only symbol pairs to nothing addable
            const std::string& name = add.clip.name;
            auto ai = animOf.find(name);
            if (ai == animOf.end()) ++unmatched;              // no behaviour animationName — emit anyway (metadata only)

            const std::string fn = havok::animdata::UniqueFileName(name, used);

            havok::animdata::ClipGenerator clip = add.clip;   // name/crop/speed/triggers verbatim from Nemesis
            clip.animation = (ai != animOf.end()) ? ai->second : std::string{};   // animationName metadata (best-effort)
            clip.animIndex.clear();                           // no hardwired index — the runtime assigns the high band
            const std::string cy = havok::animdata::EmitClipYaml(clip, {}, /*writeName*/ fn != name);
            fs::create_directories(cdir, de);
            std::ofstream(cdir / (fn + ".yaml"), std::ios::binary).write(cy.data(), static_cast<std::streamsize>(cy.size()));
            ++written;

            if (add.hasMotion) {                              // real, un-derivable root motion — keyed by clip name
                const std::string my = havok::animdata::EmitMotionSidecar(add.motion);
                fs::create_directories(mdir, de);
                std::ofstream(mdir / (fn + ".yaml"), std::ios::binary).write(my.data(), static_cast<std::streamsize>(my.size()));
            }
        }
    }
    if (unmatched) say("  " + std::to_string(unmatched) + " Nemesis clip(s) had no behaviour animationName (emitted without — metadata only).");
    return written;
}

// Minimal JSON string escaper for the flat manifest schema — escape the JSON-significant
// characters so a stray quote/backslash in a mod name can't corrupt the file.
std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:   o += c;      break;
        }
    }
    return o;
}

// Write <bundleDir>/manifest.json — the BundleManifest schema (see
// src/SKSE/Community Behaviors/hpp/BundleManifest.h). Deliberately PLAIN JSON (no comments)
// so the MO2 manager's Python stdlib `json` reads it without a JSONC shim; BR's nlohmann
// reader tolerates either. Empty version/author are written as "" (honest "unknown") rather
// than a faked number. Overwrites any existing file.
void WriteManifest(const fs::path& bundleDir, const std::string& name,
                   const std::string& version, const std::string& author,
                   const std::vector<std::string>& masters, bool compileAnimations = false) {
    std::error_code ec;
    fs::create_directories(bundleDir, ec);
    std::ofstream f(bundleDir / "manifest.json", std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << "{\n";
    f << "  \"name\": \""    << JsonEscape(name)    << "\",\n";
    f << "  \"version\": \"" << JsonEscape(version) << "\",\n";
    f << "  \"author\": \""  << JsonEscape(author)  << "\",\n";
    // OPT-IN animation compile (default false): CB conversions never compile animations — the
    // animations ride along only to donate root motion to the adsf. An author who ships animations
    // that MUST compile (True Cinematics) sets this true in their bundle's manifest.
    f << "  \"compile_animations\": " << (compileAnimations ? "true" : "false") << ",\n";
    f << "  \"masters\": [";
    for (std::size_t i = 0; i < masters.size(); ++i) {
        if (i) f << ", ";
        f << "\"" << JsonEscape(masters[i]) << "\"";
    }
    f << "]\n";
    f << "}\n";
}

// A Nemesis mod's info.ini identity, if it ships one. Real Nemesis mods carry
// <code>/info.ini with name/author/version (and site) lines; keys match
// case-insensitively and any [section] header is ignored. Missing file/keys -> empties,
// and the caller falls back to the Nemesis code as the name.
struct NemesisInfo { std::string name, author, version; };
NemesisInfo ReadNemesisInfo(const fs::path& codeDir) {
    NemesisInfo n;
    std::ifstream f(codeDir / "info.ini");
    if (!f) return n;
    auto trim = [](std::string s) {
        const auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string{};
        const auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    };
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = ToLower(trim(line.substr(0, eq)));
        const std::string val = trim(line.substr(eq + 1));
        if      (key == "name")    n.name    = val;
        else if (key == "author")  n.author  = val;
        else if (key == "version") n.version = val;
    }
    return n;
}

}  // namespace

// Read-only analysis of the active Pandora load order (for the Pandora Order tab). Uses the SAME
// helpers as the conversion (ScanCodes / LoadPandoraOrder / ScanGraphConflicts) so the tab's order +
// conflicts match what a convert will actually do. Owning-mod grouping + engine exclusion come from the
// MO2 instance when given (cosmetic — never affects order); without it, codes are ungrouped.
PandoraAnalysis AnalyzePandoraOrder(const std::string& dataDirS, const std::string& mo2InstanceS) {
    PandoraAnalysis out;
    const fs::path dataDir = dataDirS;
    const std::vector<std::string> codes = ScanCodes(dataDir);

    const std::vector<PandoraModEntry> pandora = LoadPandoraOrder(dataDir);
    out.fromPandora = !pandora.empty();
    std::unordered_map<std::string, int> pri;          // code(lower) -> priority (1 = winner)
    std::unordered_set<std::string>      inactive;
    for (const auto& pe : pandora) {
        if (pe.active) pri[ToLower(pe.code)] = pe.priority;
        else           inactive.insert(ToLower(pe.code));
    }

    // code -> owning mod + engine codes, from the MO2 instance's mods/ (optional, cosmetic).
    std::unordered_map<std::string, std::string> codeMod;
    std::unordered_set<std::string>              engineCodes;
    if (!mo2InstanceS.empty()) {
        std::error_code se;
        const fs::path mods = fs::path(mo2InstanceS) / "mods";
        if (fs::is_directory(mods, se))
            for (fs::directory_iterator it(mods, se), end; !se && it != end; it.increment(se)) {
                if (!it->is_directory()) continue;
                const fs::path nem = it->path() / "Nemesis_Engine" / "mod";
                std::error_code e2;
                if (!fs::is_directory(nem, e2)) continue;
                const bool eng = IsEngineMod(it->path());
                for (fs::directory_iterator ct(nem, e2), cend; !e2 && ct != cend; ct.increment(e2))
                    if (ct->is_directory()) {
                        const std::string c = ToLower(ct->path().filename().string());
                        codeMod.emplace(c, it->path().filename().string());
                        if (eng) engineCodes.insert(c);
                    }
            }
    }

    // Display order = winner (priority 1) FIRST; unranked codes last (scan order). Skip inactive + engine.
    std::vector<std::string> disp;
    for (const auto& c : codes) {
        const std::string cl = ToLower(c);
        if (inactive.count(cl) || engineCodes.count(cl)) continue;
        disp.push_back(c);
    }
    constexpr int kAbsent = 1 << 30;
    auto priOf = [&](const std::string& c) { const auto it = pri.find(ToLower(c)); return it == pri.end() ? kAbsent : it->second; };
    std::stable_sort(disp.begin(), disp.end(), [&](const std::string& a, const std::string& b) { return priOf(a) < priOf(b); });
    for (const auto& c : disp) {
        PandoraCodeInfo pc;
        pc.code = c;
        const auto m = codeMod.find(ToLower(c));
        pc.owningMod = (m != codeMod.end()) ? m->second : "";
        const int p = priOf(c);
        pc.priority = (p == kAbsent) ? 0 : p;
        pc.active = true;
        out.codes.push_back(std::move(pc));
    }

    // Conflicts against the merge order (winner LAST = reverse of display).
    std::vector<std::string> mergeOrder(disp.rbegin(), disp.rend());
    out.conflicts = ScanGraphConflicts(dataDir, mergeOrder);
    return out;
}

// Per-mod animation packaging (piece 1). Defined below BuildBaseBundle (needs PeekHkxKind/HkxKind/
// BakeAnimationUnit); forward-declared here so ConvertLoadOrder can call it.
static void PackageModAnimations(const Mo2Layout& mo2, const std::filesystem::path& plugins,
                                 const std::filesystem::path& stageRoot, const LogFn& log,
                                 const std::string& targetBundle);

Result ConvertLoadOrder(const Options& opt, const LogFn& log, const std::atomic<bool>& cancel) {
    Result r;
    auto say = [&](const std::string& s) { if (log) log(s); };
    const fs::path dataDir = opt.dataDir, templatesDir = opt.templatesDir, outDir = opt.outputDir;
    const fs::path baseDir = opt.baseDir;

    std::error_code ec;
    if (!fs::is_directory(dataDir, ec))      { r.error = "Data folder not found: " + opt.dataDir; return r; }
    if (!fs::is_directory(templatesDir, ec)) { r.error = "Templates folder not found: " + opt.templatesDir; return r; }

    const auto graphs = ScanGraphs(templatesDir);
    if (graphs.empty()) { r.error = "no tagfile templates (<g>.xml) in " + opt.templatesDir; return r; }
    const auto codes = ScanCodes(dataDir);
    say("Templates: " + std::to_string(graphs.size()) + " graph(s). Nemesis codes found: " +
        std::to_string(codes.size()) + ".");

    // ---- MO2-attributed discovery (enhancement; falls back to the un-attributed path) -------
    // Resolve the instance, then map each Nemesis code -> its owning installed mod, and each
    // precompiled-graph meshes-relative prefix -> the TOP-priority mod that ships it. When no
    // instance resolves both maps stay empty and everything below behaves exactly as before:
    // bundleName(code)==code, and pass 1d's precompiled derive stays anonymous (BehaviorFiles).
    std::unordered_map<std::string, std::string> codeToMod;    // lower(code)   -> modName
    std::unordered_map<std::string, std::string> prefixToMod;  // lower(prefix) -> modName (winner)
    std::unordered_map<std::string, int>         bundlePriority; // modName -> modlist rank (0 = top/winner)
    std::unordered_set<std::string>              excludedCodes;  // lower(code) — engine (Pandora/Nemesis) codes to skip
    Mo2Layout mo2;   // hoisted to function scope so the per-mod animation pass (after `plugins` below) can walk enabled mods
    {
        const fs::path inst = DeriveInstanceRoot(dataDir, opt.mo2Instance, log);
        if (inst.empty()) {
            say(opt.mo2Instance.empty()
                    ? "MO2 instance: none auto-derived — using un-attributed bundles (flat <code>.hky)."
                    : "MO2 instance: override did not resolve — using un-attributed bundles.");
        } else {
            mo2 = ResolveMo2Layout(inst);
            if (!mo2.ok) {
                say("MO2 instance '" + inst.string() + "': no enabled modlist — un-attributed bundles.");
            } else {
                say("MO2 instance: " + inst.string() + " (" + std::to_string(mo2.enabledTopFirst.size()) +
                    " enabled mod(s)). Attributing bundles to owning mods.");
                const auto contribs = DiscoverContributions(mo2, log, excludedCodes);
                for (const auto& c : contribs) {
                    bundlePriority[c.modName] = c.priority;
                    for (const auto& code : c.nemesisCodes)
                        codeToMod.emplace(ToLower(code), c.modName);   // first (top-priority) wins
                    for (const auto& hkx : c.precompiledGraphs) {
                        // meshes-relative prefix, e.g. "meshes/actors/horse/behaviors/horsebehavior.hkx"
                        std::error_code re;
                        const fs::path rel = fs::relative(hkx, mo2.modsDir / c.modName, re);
                        if (re) continue;
                        std::string key = ToLower(rel.generic_string());
                        prefixToMod.emplace(std::move(key), c.modName);  // top-priority mod wins the prefix
                    }
                }
            }
        }
    }
    // bundleName: the owning mod's name when known (groups a mod's codes + precompiled graphs into
    // one <modName>.hky), else the raw code — the un-attributed fallback.
    auto bundleName = [&](const std::string& code) -> std::string {
        const auto it = codeToMod.find(ToLower(code));
        return it == codeToMod.end() ? code : it->second;
    };

    // DEFAULT delta path = data-driven (schema). Havok/ ships next to templates/; when it loads, per-mod
    // behavior deltas go through havok::model::ConvertModDelta (schema-driven — faithful new #code$N nodes,
    // which the typed populators drop). The typed ConvertPatch is the fallback (schema dir absent, or a
    // graph the schema path errors on). Base for the merge is the graph's template tagfile (its #NNNN are
    // the canonical ids the runtime base is keyed by).
    const fs::path havokDir = templatesDir.parent_path() / "Havok";
    havok::schema::SchemaRegistry schemaReg;
    std::string schemaErr;
    const bool haveSchema = fs::is_directory(havokDir, ec) && schemaReg.LoadDir(havokDir.string(), schemaErr);
    say(haveSchema ? "Schema: loaded (data-driven delta = default)."
                   : "Schema: Havok/ not loaded (" + schemaErr + ") — using the typed delta path.");

    const fs::path plugins = outDir / "community_behaviors" / "plugins";
    // MO2-PROFILE (single-bundle) mode ships one merged bundle; every leg routes into it. (Definition
    // hoisted here so the loose-derive / char-file legs above the bundle grouping can use it too.)
    const std::string kPandora = "Pandora";

    // ── PIECE 1: package each enabled mod's loose animations into its own <modName>.hky as attributed
    // native units, the per-mod sibling of BuildBaseBundle's animation leg. Only in the MO2-attributed
    // path (we need the per-mod meshes dirs). FAIL-SAFE: a skip/fail writes no unit and the engine keeps
    // the loose .hkx — so this can never break the behavior/skeleton bundles built below. Index-bound for
    // now; bone-name binding (the membrane) is piece 2.
    // ROOT-MOTION PACKAGING (point 5): package each mod's AMR-motion animations into the bundle so their
    // root motion feeds the adsf (the runtime motionsForRoot read). MOTION-ONLY — the pose is not
    // compiled/served unless the bundle's manifest opts into compile (compile_animations, default false),
    // so this is the "decompiled animations feed the adsf root motion" path, ungated by the compile flag.
    // Single-bundle mode routes every mod's motion units into Pandora.hky; per-mod mode into each
    // <modName>.hky. Retires the old hardcoded kGatePerModAnimations release gate.
    if (mo2.ok) PackageModAnimations(mo2, plugins, outDir, log, opt.singleBundle ? kPandora : std::string());
    fs::create_directories(plugins, ec);

    // A graph's REAL meshes-relative serve path (base-map ground truth) — bound after baseMaps loads
    // (below), called from the per-bundle loop (later still). Fixes the humanoid hardcode: a Nemesis
    // patch against a non-humanoid templated graph (horsebehavior) files its delta under its own actor
    // (actors/horse) instead of actors/character, so the runtime actually applies it. kBehSub[1st]
    // fallback for a new/custom graph not present in the base.
    std::function<std::string(const std::string& g, bool firstPerson)> graphServePathOf;

    auto binOf   = [&](const std::string& g) { return (templatesDir / (g + ".hkx")).string(); };
    auto xmlOf   = [&](const std::string& g) { return (templatesDir / (g + ".xml")).string(); };
    auto unitOut = [&](const std::string& bundle, const std::string& g) {
        return (plugins / (bundle + ".hky") / fs::path(graphServePathOf(g, false))).string();
    };
    std::unordered_map<std::string, std::string> baseXmlCache;   // graph -> its template tagfile text (read once)
    auto baseXmlOf = [&](const std::string& g) -> const std::string& {
        auto it = baseXmlCache.find(g);
        if (it != baseXmlCache.end()) return it->second;
        std::ifstream f(xmlOf(g), std::ios::binary);
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return baseXmlCache.emplace(g, std::move(s)).first->second;
    };

    // FIRST-PERSON: mods patch _1stperson/<graph> against the FIRST-PERSON base — a DIFFERENT graph than
    // the same-named third-person one (e.g. first-person 0_master has 1903 objects, not 2418), with its own
    // canonical #NNNN template under templates/_1stperson/. Dropping these half-wires the FirstPerson
    // project and universally A-poses (the SkyParkour first-person case). First-person conversion is
    // schema-only (there is no typed first-person vanilla-binary path).
    const auto fpGraphs = ScanGraphs(templatesDir / "_1stperson");
    auto fpXmlOf   = [&](const std::string& g) { return (templatesDir / "_1stperson" / (g + ".xml")).string(); };
    auto fpUnitOut = [&](const std::string& bundle, const std::string& g) {
        return (plugins / (bundle + ".hky") / fs::path(graphServePathOf(g, true))).string();
    };
    std::unordered_map<std::string, std::string> fpBaseXmlCache;
    auto fpBaseXmlOf = [&](const std::string& g) -> const std::string& {
        auto it = fpBaseXmlCache.find(g);
        if (it != fpBaseXmlCache.end()) return it->second;
        std::ifstream f(fpXmlOf(g), std::ios::binary);
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return fpBaseXmlCache.emplace(g, std::move(s)).first->second;
    };
    if (!fpGraphs.empty())
        say("First-person: " + std::to_string(fpGraphs.size()) + " template(s) — _1stperson deltas enabled.");

    // Forward declaration — the typed fallback's vanilla-binary source. Its definition (and the
    // baseArc/alignClean/vanBinCache chain it needs) lives below; convertBehaviorGraph only CALLS
    // it from the per-code loop (later still), so a std::function bound after this point is safe.
    std::function<std::string(const std::string&)> getVanillaBin;

    // Convert ONE third-person behavior graph's patch dir(s) into `outHkx` — schema path by default,
    // typed ConvertPatch as the fallback (same policy as before; see the batch loop's 2a). `patchDirs` is a
    // LIST so the merged path can pass every mod's dir for this graph in one call; the delta path passes a
    // single dir. `label` prefixes log lines (a Nemesis code, or "Pandora"). Returns true if a delta landed.
    // Shared by the per-code delta path and the future merged/single-mod paths — the one behavior source.
    auto convertBehaviorGraph = [&](const std::string& g, const std::vector<std::string>& patchDirs,
                                    const std::string& label, const std::string& outHkx) -> bool {
        if (haveSchema) {
            const std::string& baseXml = baseXmlOf(g);
            if (!baseXml.empty()) {
                const auto md = havok::model::ConvertModDelta(baseXml, patchDirs, schemaReg, outHkx);
                if (md.ok) {
                    ++r.deltas;
                    for (const auto& w : md.warnings) say("      " + label + "/" + g + ": " + w);
                    return true;
                }
                say("  " + label + "/" + g + ": schema delta failed (" + md.error + ") — falling back to typed");
            }
        }
        const std::string vanBin = getVanillaBin(g);
        if (vanBin.empty()) {
            ++r.skipped;
            say("  " + label + "/" + g + ": no vanilla source (base unit absent + no template) — skipped");
            return false;
        }
        const auto res = havok::sct::ConvertPatch(vanBin, xmlOf(g), patchDirs, "", outHkx, "");
        if (res.ok) {
            ++r.deltas;
            if (res.skippedMismatch > 0 || !res.unsupportedClasses.empty()) {
                std::string w = "  " + label + "/" + g + ": DROPPED";
                if (res.skippedMismatch) w += " " + std::to_string(res.skippedMismatch) + " override(s) (class/source drift)";
                if (!res.unsupportedClasses.empty()) {
                    w += " | unsupported class(es):";
                    for (const auto& u : res.unsupportedClasses) w += " " + u;
                }
                say(w);
            }
            for (const auto& warn : res.warnings) say("      " + label + "/" + g + ": " + warn);
            return true;
        }
        ++r.skipped; say("  " + label + "/" + g + ": behavior FAILED — " + res.error);
        return false;
    };

    // Convert ONE first-person behavior graph's patch dir(s) into `outHkx` (schema-only — no typed
    // first-person source). Same list/label contract as convertBehaviorGraph.
    auto convertFirstPersonGraph = [&](const std::string& g, const std::vector<std::string>& patchDirs,
                                       const std::string& label, const std::string& outHkx) -> bool {
        if (!haveSchema) {
            ++r.skipped;
            say("  " + label + "/_1stperson/" + g + ": first-person needs the schema path (Havok/ absent) — skipped");
            return false;
        }
        const std::string& baseXml = fpBaseXmlOf(g);
        if (baseXml.empty()) {
            ++r.skipped;
            say("  " + label + "/_1stperson/" + g + ": no first-person template — skipped");
            return false;
        }
        const auto md = havok::model::ConvertModDelta(baseXml, patchDirs, schemaReg, outHkx);
        if (md.ok) {
            ++r.deltas;
            for (const auto& w : md.warnings) say("      " + label + "/_1stperson/" + g + ": " + w);
            return true;
        }
        ++r.skipped; say("  " + label + "/_1stperson/" + g + ": first-person delta FAILED — " + md.error);
        return false;
    };

    // ── Vanilla behavior binary source for the Nemesis path (pass 2a) ─────────────────
    // Pass 2a used to read the vanilla binary from templates/<g>.hkx. It now SOURCES that
    // binary from the shipped Skyrim.hky base: compile the graph's base unit
    // (meshes/actors/character/behaviors/<g>.hkx) and feed the recompiled binary to the
    // UNCHANGED ConvertPatch — one canonical vanilla source, the very bytes the deltas merge
    // onto. templates/<g>.xml stays the oracle's alignment source; only WHERE the binary
    // comes from changes. A per-graph FIDELITY GATE keeps it safe: recompiling a base unit
    // whose YAML has an inline sub-object with id != name (the root 0_master's empty
    // hkbBoneWeightArray blender children) drops those objects, so the recompiled binary no
    // longer aligns to the tagfile. AlignTagfile detects that (class/ref-count mismatch) and
    // such a graph FALLS BACK to templates/<g>.hkx. Base binaries are compiled once per graph
    // and cached (reused across every Nemesis code that patches the graph).
    std::shared_ptr<havok::model::HkyArchive> baseArc;
    {
        const fs::path baseHky = dataDir / "community_behaviors" / "plugins" / "Skyrim.hky";
        std::string herr;
        baseArc = havok::model::HkyArchive::LoadFromFile(baseHky.string(), herr);
        if (!baseArc)
            say("  note: base bundle not readable at " + baseHky.string() +
                " (install Community Behaviors first) — Nemesis path falls back to templates/<g>.hkx, "
                "loose-behavior derive skipped. " + herr);
    }
    std::unordered_set<std::string> baseUnits;   // behavior unit prefixes present in Skyrim.hky
    if (baseArc)
        for (const auto& u : baseArc->units())
            if (u.kind == havok::model::HkyArchive::UnitKind::Behavior) baseUnits.insert(u.prefix);

    // Base-derived resolution maps (ground truth = Skyrim.hky). Defined here (before the loose legs +
    // the per-bundle loop + the animationnames post-pass) so every roster leg routes by real actor/
    // character. See PatchPlan.h — the same maps back the PatchPlan prelude.
    bconv::BaseMaps baseMaps;
    if (baseArc) baseMaps = bconv::BuildBaseMaps(*baseArc);

    // character-file stem (lower) -> its meshes-relative serve path ("horse" -> ".../characters/horse.hkx").
    std::unordered_map<std::string, std::string> charStemToServe;
    for (const auto& [actor, chars] : baseMaps.actorCharacters)
        for (const auto& cp : chars) charStemToServe[ToLower(fs::path(cp).stem().string())] = cp;

    // The character unit serve path(s) a graph's clip roster belongs to: the graph's actor's characters,
    // filtered to the graph's namespace (third-person graphs -> defaultmale/defaultfemale; first-person
    // graphs -> firstperson). Empty when the graph/actor isn't in the base (a new/custom graph).
    // Character serve path(s) for an EXPLICIT actor path (e.g. "actors/horse"), namespace-filtered.
    // The loose/precompiled path uses this with the graph's REAL actor prefix (leg 1d), sidestepping the
    // stem-collision in the graph map (horsebehavior lives at both actors/character and actors/horse).
    auto charServesForActorPath = [&](const std::string& actorPath, bool firstPerson) -> std::vector<std::string> {
        const auto ci = baseMaps.actorCharacters.find(actorPath);
        if (ci == baseMaps.actorCharacters.end()) return {};
        std::vector<std::string> out;
        for (const auto& cp : ci->second) {
            const bool cFp = cp.find("_1stperson") != std::string::npos;
            if (cFp == firstPerson) out.push_back(cp);
        }
        return out;
    };
    // Character serve path(s) for a TEMPLATED graph stem (resolves via the base map, which prefers
    // actors/character on a collision — so a Nemesis horsebehavior patch routes to the rider, not the horse).
    auto charServesFor = [&](const std::string& graphStemLower, bool firstPerson) -> std::vector<std::string> {
        const auto& actorMap = firstPerson ? baseMaps.fpGraphActorPath : baseMaps.graphActorPath;
        const auto ai = actorMap.find(graphStemLower);
        if (ai == actorMap.end() || ai->second.empty()) return {};
        return charServesForActorPath(ai->second, firstPerson);
    };

    // Bind the serve-path resolver forward-declared above unitOut/fpUnitOut (now that baseMaps exists).
    graphServePathOf = [&](const std::string& g, bool firstPerson) -> std::string {
        const auto& m = firstPerson ? baseMaps.fpGraphServePath : baseMaps.graphServePath;
        if (const auto it = m.find(ToLower(g)); it != m.end()) return it->second;
        return std::string(firstPerson ? kBehSub1st : kBehSub) + "/" + g + ".hkx";  // new/custom graph fallback
    };

    const fs::path baseBinTmp = fs::temp_directory_path(ec) / "sct_conv_basebin";
    fs::remove_all(baseBinTmp, ec);
    fs::create_directories(baseBinTmp, ec);

    // FIDELITY GATE — a recompiled base binary is safe to source from iff it is STRUCTURALLY
    // ISOMORPHIC to the tagfile: co-DFS from the shared root maps every binary object to a
    // tagfile #NNNN of the same class with the same out-ref count (0 class / 0 ref-count
    // mismatch), and every binary object is reached (mapped == binObjs). That guarantees the
    // ConvertPatch decompile keyed by this alignment reproduces the tagfile node structure —
    // i.e. an IDENTICAL vanilla base to templates/<g>.hkx (verified byte-for-byte across the
    // vanilla corpus by `havok-core-cli basefidelity`). Benign deltas are tolerated: object-count
    // differences with full mapping and 0 mismatches (mt_behavior/horsebehavior re-share nodes),
    // and `conflict` (an id reached twice, consistently). The one graph this rejects is 0_master —
    // its base unit YAML has inline sub-objects (empty hkbBoneWeightArray blender children) with
    // id != name that the compiler drops, so the recompiled binary is genuinely missing objects
    // (7 ref-count mismatches) and it falls back to the retained templates/0_master.hkx. It also
    // conservatively rejects 1hm_behavior, whose tagfile carries an INHERENT oracle quirk (a
    // shared clip-trigger/binding pair the co-DFS can't split) that the template binary reproduces
    // too — safe to base-source in principle, but indistinguishable from a real drop without the
    // template on hand, so it keeps its template as well. Both kept templates are the only .hkx
    // still shipped; every other graph is base-sourced from Skyrim.hky.
    auto alignClean = [](const std::vector<std::uint8_t>& bin, const std::string& xmlText) -> bool {
        try {
            havok::PackFileDeserializer des;
            havok::BinaryReaderEx br(false, true, bin);
            des.DeserializePartially(br);
            std::uint32_t root = 0xFFFFFFFFu;
            for (const auto& [o, c] : des.ListObjects()) if (c == "hkRootLevelContainer") { root = o; break; }
            if (root == 0xFFFFFFFFu) return false;
            havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
            des.ConstructVirtualClass(dr, root);
            const auto res = havok::sct::AlignTagfile(des, xmlText);
            return res.ok && res.classMism == 0 && res.refCountMism == 0 && res.mapped == res.binObjs;
        } catch (...) { return false; }
    };

    // graph -> vanilla binary path fed to ConvertPatch (base-sourced when the gate passes,
    // templates/<g>.hkx otherwise; "" when neither is available). Cached per graph.
    std::unordered_map<std::string, std::string> vanBinCache;
    getVanillaBin = [&](const std::string& g) -> std::string {
        if (auto it = vanBinCache.find(g); it != vanBinCache.end()) return it->second;
        std::string chosen;
        const std::string prefix = graphServePathOf(g, false);   // real serve path (horsebehavior -> actors/horse)
        if (baseArc && baseUnits.count(prefix)) {
            try {
                auto data = havok::model::YamlBehaviorLoader::LoadMerged({ baseArc->source(prefix) });
                const auto cr = havok::sct::CompileBehavior(data);
                std::string werr;
                const fs::path outBin = baseBinTmp / (g + ".hkx");
                if (cr.ok && havok::sct::WriteHavokFile(outBin.string(), cr.bytes, &werr)) {
                    std::ifstream xf(xmlOf(g), std::ios::binary);
                    const std::string xml((std::istreambuf_iterator<char>(xf)), std::istreambuf_iterator<char>());
                    if (alignClean(cr.bytes, xml)) {
                        chosen = outBin.string();   // base-sourced from Skyrim.hky
                    } else {
                        std::error_code re; fs::remove(outBin, re);
                        say("  " + g + ": base unit not tagfile-clean — using retained templates/" + g + ".hkx");
                    }
                } else {
                    say("  " + g + ": base unit compile failed (" + (cr.ok ? werr : cr.error) +
                        ") — using templates/" + g + ".hkx");
                }
            } catch (const std::exception& e) {
                say("  " + g + ": base unit load threw (" + std::string(e.what()) +
                    ") — using templates/" + g + ".hkx");
            }
        }
        if (chosen.empty()) {   // fall back to the shipped template binary if present
            std::error_code te;
            if (fs::is_regular_file(fs::path(binOf(g)), te)) chosen = binOf(g);
        }
        vanBinCache.emplace(g, chosen);
        return chosen;
    };

    // The converter no longer emits its OWN Skyrim.hky base. The full-corpus vanilla master
    // ships with Community Behaviors (Data/Community Behaviors/community_behaviors/plugins/Skyrim.hky, built
    // by `--build-base`) and is THE one base every mod .hky layers over at runtime. Keeping the
    // converter out of the master business means (a) there is exactly ONE canonical Skyrim.hky —
    // a converter-regenerated one could subtly drift from the shipped one (different decompile
    // path, index base), and (b) no two mods write community_behaviors/plugins/Skyrim.hky into the
    // MO2 VFS (that collision is a known crash). Mod behavior deltas below still diff against the
    // vanilla TEMPLATES — the same vanilla the master is built from — and the runtime merge is
    // NAME-based, so each delta binds to the shipped master's identically-named nodes. Only the
    // "Skyrim" references in loadorder.txt + the mod manifests remain (they name the base bundle,
    // which is installed separately with BR).
    const auto charTemplates = ScanCharacterTemplates(templatesDir);

    // 1c) LOOSE character-file replacements — some mods ship a whole character .hkx
    //     instead of a Nemesis char patch (BFCO's defaultmale.hkx, HorsePower's
    //     horse.hkx); their roster additions live IN that file. Diff the VFS-winning
    //     loose file against the vanilla template and emit the additions as a single
    //     roster delta in a synthetic CharacterFiles.hky bundle: animationnames\<stem>.txt.
    //     The runtime folds that into the compiled character's roster (author-drop path)
    //     AND the set-data guard reads the same file — one form, two consumers. Without
    //     this, serving a compiled character would DROP the replacing mod's roster.
    bool charFilesAny = false;
    if (!charTemplates.empty()) {
        const fs::path cfBundle = plugins / ((opt.singleBundle ? kPandora : std::string("CharacterFiles")) + ".hky");
        for (const auto& rel : charTemplates) {
            std::error_code we;
            const fs::path winner = dataDir / "meshes" / fs::path(rel);
            const fs::path tmpl   = templatesDir / "characters" / fs::path(rel);
            if (!fs::is_regular_file(winner, we)) continue;
            // fast path: identical size -> almost certainly vanilla; skip the decompile
            if (fs::file_size(winner, we) == fs::file_size(tmpl, we)) continue;

            const fs::path scratch = outDir / ".chardiff" / fs::path(rel).stem();
            std::vector<std::uint8_t> bytes;
            std::string rerr;
            if (!havok::sct::ReadHavokFile(winner.string(), bytes, &rerr)) {
                ++r.skipped; say("  " + rel + ": loose-file READ FAILED — " + rerr); continue;
            }
            const auto dres = havok::sct::DecompileToDir(bytes, scratch.string());
            if (!(dres.ok && dres.kind == "character")) {
                ++r.skipped;
                say("  " + rel + ": loose-file DECOMPILE FAILED — " +
                    (dres.ok ? ("kind: " + dres.kind) : dres.error));
                fs::remove_all(scratch, we);
                continue;
            }
            // additions = winner roster lines not in the vanilla unit's roster.
            // Both rosters are the decompiled unit's data/animations.yaml — a block sequence of
            // single-quoted LITERAL paths (the decompiler no longer XML-escapes), so no UnescapeXml.
            auto readRoster = [](const fs::path& unitDir) {
                std::vector<std::string> lines;
                std::ifstream f(unitDir / "data" / "animations.yaml");
                std::string line;
                while (std::getline(f, line)) {
                    std::string t = line;
                    while (!t.empty() && (t.back() == '\r' || t.back() == '\n' || t.back() == ' ' || t.back() == '\t')) t.pop_back();
                    const std::size_t s = t.find_first_not_of(" \t");
                    if (s == std::string::npos) continue;
                    t = t.substr(s);
                    if (t.empty() || t[0] == '#') continue;
                    if (t.rfind("- ", 0) == 0) t = t.substr(2);
                    else if (t[0] == '-') t = t.substr(1);
                    const std::size_t s2 = t.find_first_not_of(" \t");
                    if (s2 != std::string::npos) t = t.substr(s2);
                    if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') {
                        const std::string inner = t.substr(1, t.size() - 2); std::string un;
                        for (std::size_t i = 0; i < inner.size(); ++i)
                            if (inner[i] == '\'' && i + 1 < inner.size() && inner[i + 1] == '\'') { un += '\''; ++i; }
                            else un += inner[i];
                        t = std::move(un);
                    }
                    if (!t.empty()) lines.push_back(std::move(t));
                }
                return lines;
            };
            // Vanilla roster to diff against — decompile the template on demand (the base is no
            // longer emitted to Skyrim.hky, so read the roster straight from the template).
            std::vector<std::string> vanilla;
            {
                const fs::path vscratch = outDir / ".chardiff" / (fs::path(rel).stem().string() + "__vanilla");
                std::vector<std::uint8_t> vbytes;
                std::string               vrerr;
                if (havok::sct::ReadHavokFile(tmpl.string(), vbytes, &vrerr) &&
                    havok::sct::DecompileToDir(vbytes, vscratch.string()).ok)
                    vanilla = readRoster(vscratch);
                fs::remove_all(vscratch, we);
            }
            const auto modded = readRoster(scratch);
            std::unordered_set<std::string> have;
            have.reserve(vanilla.size() * 2);
            for (const auto& a : vanilla) have.insert(ToLower(a));
            std::vector<std::string> adds;
            for (const auto& a : modded) if (!have.count(ToLower(a))) adds.push_back(a);
            fs::remove_all(scratch, we);
            if (adds.empty()) continue;   // replaced file, but same roster — nothing to carry

            // Native roster delta at the character unit's real serve path (rel mirrors the meshes tree,
            // e.g. "actors/horse/characters/horse.hkx") — data/animations.yaml the runtime unions on top
            // of the base character, replacing the bundle-root animationnames/<stem>.txt shim.
            const std::string charServe = "meshes/" + fs::path(rel).generic_string();
            const int rAdded = AddRosterToCharacterUnit(cfBundle, charServe, adds);
            if (rAdded <= 0) continue;
            ++r.charDeltas; charFilesAny = true;
            say("  CharacterFiles: " + rel + " — " + std::to_string(rAdded) +
                " roster addition(s) (loose replacement).");
        }
        std::error_code te;
        fs::remove_all(outDir / ".chardiff", te);
    }

    // 1d) LOOSE BEHAVIOR replacements — some mods ship a whole precompiled behavior GRAPH
    //     (HorsePower's actors/horse/behaviors/horsebehavior.hkx) instead of a Nemesis patch,
    //     so a delta-only runtime never sees their changes. AUTO-DETECT them: walk every
    //     behavior graph in the shipped Skyrim.hky base (the vanilla source, in the exact
    //     numbering DeriveDelta targets), and for each whose VFS-winning file differs, DERIVE a
    //     delta (structural-identity match, keyed by the base's numeric ids) into a synthetic
    //     BehaviorFiles.hky. No per-mod template needed — the base IS the vanilla reference.
    //     Character behaviors (actors/character/) are Nemesis-owned (pass 2) and skipped here.
    //     ATTRIBUTION: when discovery resolved an owner for a precompiled graph's prefix, the
    //     derived delta goes into that mod's <modName>.hky (grouped with its Nemesis leg — the
    //     two-disjoint-layers case, HorsePower's jump/sprint binary + hpmhr attack-followup
    //     patch). With no owner it lands in the anonymous BehaviorFiles.hky exactly as before.
    bool behFilesAny = false;                     // the anonymous fallback bundle got content
    std::set<std::string> precompiledBundles;     // attributed <modName> bundles that got a precompiled delta
    if (baseArc) {
            const fs::path tmpDir   = fs::temp_directory_path(ec);

            // Bone-name resolution for the base compile. A creature's vanilla behavior
            // (horse's horsebehavior.hkx, canine's, …) references hkbBoneIndexArray by NAME
            // (`18_bones`, `91_bones`), which BehaviorBuilder resolves against data.boneNames.
            // The RUNTIME injects a per-actor bone table at Init from the served skeleton YAML;
            // the offline converter must do the same here or the base compile throws "bone list
            // is EMPTY" and the whole loose-derive is skipped (HorsePower's horsebehavior never
            // converts — BR-34 follow-up). Same source: the Skyrim.hky base carries every vanilla
            // skeleton as a YAML tree; find the actor's skeleton unit (a bonelist.yaml under
            // meshes/actors/<actor>/) and assemble its names. Mirrors Resolver::ActorPathOf + the
            // Init bone-table producer. Cached per actor path (negative cache included).
            std::map<std::string, std::vector<std::string>> boneNameCache;
            auto actorPathOf = [](const std::string& prefix) -> std::string {
                std::string k = ToLower(prefix);
                const auto ap = k.find("actors/");
                if (ap == std::string::npos) return {};
                const std::string rest = k.substr(ap + 7);
                for (const char* seg : { "/behaviors/", "/characters/" })
                    if (const auto b = rest.find(seg); b != std::string::npos) return rest.substr(0, b);
                return rest.substr(0, rest.find('/'));   // fallback: first segment
            };
            auto boneNamesForActor = [&](const std::string& actor) -> const std::vector<std::string>& {
                static const std::vector<std::string> kEmpty;
                if (actor.empty()) return kEmpty;
                if (const auto cit = boneNameCache.find(actor); cit != boneNameCache.end()) return cit->second;
                std::vector<std::string>& names = boneNameCache[actor];   // insert (empty = negative cache)
                std::string unit;   // first skeleton unit under the actor subtree (filesUnder is sorted)
                for (const std::string& f : baseArc->filesUnder("meshes/actors/" + actor + "/"))
                    if (const auto b = f.rfind("/bonelist.yaml"); b != std::string::npos && b + 14 == f.size()) {
                        unit = f.substr(0, b); break;
                    }
                if (unit.empty()) return names;
                const std::string bl = baseArc->file(unit + "/bonelist.yaml").value_or("");
                const auto norm = baseArc->filesUnder(unit + "/bones/");
                const auto orig = baseArc->filesUnderOrig(unit + "/bones/");
                std::vector<std::pair<std::string, std::string>> boneFiles;
                for (std::size_t i = 0; i < norm.size() && i < orig.size(); ++i) {
                    if (norm[i].size() < 5 || norm[i].compare(norm[i].size() - 5, 5, ".yaml") != 0) continue;
                    if (auto t = baseArc->file(norm[i]))
                        boneFiles.emplace_back(fs::path(orig[i]).stem().string(), *t);
                }
                if (bl.empty() && boneFiles.empty()) return names;
                havok::skeleton::SkeletonData sk; std::string serr;
                if (!havok::skeleton::LoadSkeletonYamlFromTexts(bl, boneFiles, sk, &serr)) return names;
                for (const auto& bn : sk.bones) names.push_back(bn.name);
                return names;
            };

            for (const auto& u : baseArc->units()) {
                if (cancel) { r.error = "cancelled"; return r; }
                if (u.kind != havok::model::HkyArchive::UnitKind::Behavior) continue;
                if (u.prefix.rfind("meshes/actors/character/", 0) == 0) continue;   // Nemesis-owned
                std::error_code we;
                const fs::path winner = dataDir / fs::path(u.prefix);               // VFS-winning file
                if (!fs::is_regular_file(winner, we)) continue;                      // graph not present

                // Owning mod for this precompiled prefix (top-priority shipper), else anonymous.
                const auto pm = prefixToMod.find(ToLower(fs::path(u.prefix).generic_string()));
                const bool        attributed = pm != prefixToMod.end();
                const std::string bname      = opt.singleBundle ? kPandora : (attributed ? pm->second : std::string("BehaviorFiles"));
                const fs::path    outBundle  = plugins / (bname + ".hky");

                // Base numbering ORACLE for the derive: the delta's matched ids must equal the SHIPPED
                // base master's #NNNN. For these no-template graphs the base master IS a straight
                // decompile of the vanilla binary, so deriving against the vanilla .hkx gives matched
                // ids that line up exactly. A compile/decompile ROUND-TRIP of the base master does NOT
                // preserve #NNNN (only names survive it) — that desynced every override and crashed
                // horsebehavior. Prefer the vanilla binary from SKYRIM_DATASOURCE (the unpacked vanilla
                // root); fall back to the round-trip only when it is absent (degraded — new nodes are
                // still namespaced so they can't collide, but matched overrides may misalign).
                // The loose-derive needs the pristine VANILLA binary as the numbering ORACLE: its read-order
                // is what the base master was decompiled by, so matched overrides land on the base's ids. A
                // compile/decompile round-trip of the base master does NOT preserve that order (only names
                // survive) — there is NO safe fallback. Without SKYRIM_DATASOURCE, skip the loose-derive
                // rather than emit a delta whose overrides misalign the base (the horsebehavior crash class).
                fs::path vanBin;
                if (const char* ds = std::getenv("SKYRIM_DATASOURCE")) {
                    const fs::path cand = fs::path(ds) / u.prefix;   // prefix is meshes-relative
                    if (fs::is_regular_file(cand, we)) vanBin = cand;
                }
                if (vanBin.empty()) {
                    ++r.skipped;
                    say("  " + bname + ": " + u.prefix + " — no SKYRIM_DATASOURCE vanilla; skipping loose-derive "
                        "(a round-tripped base would misalign the delta).");
                    continue;
                }

                const fs::path unit = outBundle / fs::path(u.prefix);
                // Mod namespace code for this bundle's NEW derived nodes ("<code>$N"). Short + unique:
                // an alnum-lowercased slug of the bundle name (<=8 chars) + a 4-hex hash of the full
                // name, so two mods never share a code and the id stays filename-safe/MAX_PATH-friendly.
                std::string slug;
                for (char c : ToLower(bname)) { if (std::isalnum(static_cast<unsigned char>(c))) slug += c; if (slug.size() >= 8) break; }
                unsigned h16 = static_cast<unsigned>(std::hash<std::string>{}(bname) & 0xFFFFu);
                std::string hx(4, '0');
                for (int i = 3; i >= 0; --i) { hx[i] = "0123456789abcdef"[h16 & 0xF]; h16 >>= 4; }
                const std::string modCode = slug + hx;
                // SCHEMA loose-derive (read-order) when the shared registry is armed — matches the schema
                // base decompile (BuildBaseBundle's no-template leg), so matched overrides land on the base's
                // read-order ids. If the registry is absent the base was decompiled TYPED too (same gate),
                // so derive typed to stay aligned — never mix schema base with a typed delta.
                havok::model::LooseDeriveResult res;
                if (havok::schema::SchemaRegistry* sreg = havok::schema::SharedRegistry()) {
                    std::vector<std::uint8_t> vb, mb; std::string re;
                    if (havok::sct::ReadHavokFile(vanBin.string(), vb, &re) &&
                        havok::sct::ReadHavokFile(winner.string(), mb, &re))
                        res = havok::model::DeriveLooseBehaviorDeltaSchema(vb, mb, modCode, *sreg, unit.string());
                    else res.error = "read: " + re;
                } else {
                    const auto tr = havok::sct::DeriveLooseBehaviorDelta(vanBin.string(), winner.string(), unit.string(), modCode);
                    res.ok = tr.ok; res.error = tr.error; res.matched = tr.matched; res.added = tr.added;
                    res.changedNodes = tr.changedNodes; res.newNodes = tr.newNodes; res.removedFromBase = tr.removedFromBase;
                }
                if (!res.ok) { ++r.skipped; say("  " + bname + ": " + u.prefix + " — DERIVE FAILED: " + res.error); continue; }
                if (res.changedNodes == 0 && res.newNodes == 0) { fs::remove_all(unit, we); continue; }  // vanilla — nothing to carry
                ++r.deltas;
                if (attributed) precompiledBundles.insert(bname); else behFilesAny = true;
                say("  " + bname + ": " + u.prefix + " — " + std::to_string(res.changedNodes) + " changed + " +
                    std::to_string(res.newNodes) + " new node(s) (" + std::to_string(res.matched) +
                    " matched to base ids, " + std::to_string(res.added) + " new).");
                if (res.removedFromBase)
                    say("    note: " + std::to_string(res.removedFromBase) +
                        " vanilla node(s) absent in the mod graph (removal isn't expressible; left as base).");

                // ROSTER (Phase 1): a precompiled loose graph (the horse's real ship path) carries its
                // new clips ONLY in the binary — the derive above emits the node delta but no roster, so
                // without this the new animations never enter the character's animationNames and bind to
                // nothing. Read the loose graph's clip animationNames and union them into the graph's own
                // character unit(s) (actors/horse -> characters/horse.hkx) as a data/animations.yaml delta.
                {
                    // Route by the precompiled graph's REAL actor prefix (actors/horse), not the stem
                    // map — the horse creature's roster belongs to the horse character, and the stem
                    // "horsebehavior" collides with the rider graph in the base map.
                    const std::string actorPath = bconv::ActorOfServePath(ToLower(u.prefix));
                    const std::vector<std::string> serves = charServesForActorPath(actorPath, /*firstPerson*/ false);
                    if (!serves.empty()) {
                        LooseBehaviorRefs refs;
                        if (ReadLooseBehaviorRefs(winner.string(), refs) && !refs.animationNames.empty()) {
                            int rAdded = 0;
                            for (const auto& s : serves) rAdded += AddRosterToCharacterUnit(outBundle, s, refs.animationNames);
                            if (rAdded > 0)
                                say("    roster: " + std::to_string(refs.animationNames.size()) +
                                    " clip animation(s) -> " + std::to_string(serves.size()) +
                                    " character(s), " + std::to_string(rAdded) + " new roster line(s).");
                        }
                    }
                }
            }
    }

    // 2) Per-mod bundles — each Nemesis code -> its own <code>.hky carrying, together, its
    //    behavior graph deltas + set-data (split form) + anim-data (verbatim). Shipping all
    //    three in one bundle keeps the moveset table + roster consistent with the behaviors.
    say("");
    say("== Mod bundles ==");
    std::vector<std::string> emitted;   // BUNDLE (mod) names that produced ANY contribution (for loadorder)

    // GROUP codes by their owning BUNDLE (mod when attributed, else the code itself). A mod with
    // several Nemesis codes (TDM: tdmh+tdmlen+tdmv) MUST have them MERGED into one bundle: each
    // code's ConvertModDelta writes the SAME shared unit files (additive.yaml, graphdata, base-node
    // overrides), so converting them one-at-a-time last-writer-clobbers all but the last — dropping
    // e.g. tdmlen's `TDM_Pitch` variable declaration, which 0_master then references undeclared →
    // compile fail → universal A-pose. Merging all a bundle's codes' patch dirs in ONE convert per
    // graph unions their added vocab correctly. Excluded engine (Pandora/Nemesis) codes are dropped.
    // ScanCodes order is preserved within each bundle (deterministic override-merge order).
    std::vector<std::string>                                  bundleOrder;   // first-seen order
    std::unordered_map<std::string, std::vector<std::string>> bundleCodes;   // bundle -> its codes
    for (const auto& code : codes) {
        if (excludedCodes.count(ToLower(code))) continue;   // engine scaffolding — skip entirely
        const std::string bn = bundleName(code);
        auto it = bundleCodes.find(bn);
        if (it == bundleCodes.end()) { bundleOrder.push_back(bn); bundleCodes.emplace(bn, std::vector<std::string>{ code }); }
        else it->second.push_back(code);
    }

        // MO2-PROFILE mode: collapse EVERY code into one "Pandora" bundle, ordered by PANDORA'S OWN behavior
    // load order (Pandora_Engine/ActiveMods.json — the priority the user set in Pandora's UI), NOT MO2.
    // We are replicating Pandora for the pandora.hky. ConvertModDelta applies patches in LIST order,
    // later overriding earlier, so the WINNER (Pandora priority 1) must merge LAST → sort by priority
    // DESCENDING. Codes absent from ActiveMods sort FIRST (lowest precedence, scan order among them).
    // Fallback when no ActiveMods.json: ScanCodes order (already sorted) — no MO2 dependency. Inactive
    // codes (active:false) are excluded, mirroring Pandora. This turns the per-bundle loop below into a
    // single unified delta per graph; loose-derive / char-file / FNIS legs also route into Pandora.hky.
    if (opt.singleBundle) {
        const std::vector<PandoraModEntry> pandora = LoadPandoraOrder(dataDir);
        std::unordered_map<std::string, int> pandoraPri;   // code(lower) -> priority (1 = winner)
        std::unordered_set<std::string>      pandoraInactive;
        for (const auto& pe : pandora) {
            if (pe.active) pandoraPri[ToLower(pe.code)] = pe.priority;
            else           pandoraInactive.insert(ToLower(pe.code));
        }
        std::vector<std::string> all;
        int skippedInactive = 0;
        for (const auto& code : codes) {
            if (excludedCodes.count(ToLower(code))) continue;                 // engine scaffolding
            if (pandoraInactive.count(ToLower(code))) { ++skippedInactive; continue; }  // Pandora-disabled
            all.push_back(code);
        }
        if (!pandoraPri.empty()) {
            constexpr int kAbsent = 1 << 30;   // not in ActiveMods -> lowest precedence (merges first)
            auto priOf = [&](const std::string& c) -> int {
                const auto it = pandoraPri.find(ToLower(c));
                return it == pandoraPri.end() ? kAbsent : it->second;
            };
            std::stable_sort(all.begin(), all.end(),
                             [&](const std::string& a, const std::string& b) { return priOf(a) > priOf(b); });
            say("Pandora mode: order from Pandora_Engine/ActiveMods.json (" +
                std::to_string(pandoraPri.size()) + " ranked code(s)" +
                (skippedInactive ? ", " + std::to_string(skippedInactive) + " inactive skipped" : "") +
                ", winner last).");
        } else {
            say("Pandora mode: no ActiveMods.json — using scan order (winner last). Tune it in the UI.");
        }

        // User override (Pandora Order tab): reorder by the tuned MOD order (top = winner). Group each
        // code under its owning mod and sort the groups so the winning mod merges LAST; codes keep their
        // Pandora order WITHIN a mod (stable). Codes whose mod isn't in the list (or is unattributed)
        // sort first (lowest precedence). Needs MO2 attribution (codeToMod); no-op without it.
        if (!opt.pandoraModOrder.empty() && !codeToMod.empty()) {
            std::unordered_map<std::string, int> modRank;   // mod(lower) -> user rank (0 = top = winner)
            for (int i = 0; i < (int)opt.pandoraModOrder.size(); ++i)
                modRank[ToLower(opt.pandoraModOrder[i])] = i;
            constexpr int kUnranked = 1 << 30;              // mod not in the user list -> merges first
            auto modRankOf = [&](const std::string& c) -> int {
                const auto m = codeToMod.find(ToLower(c));
                if (m == codeToMod.end()) return kUnranked;
                const auto r = modRank.find(ToLower(m->second));
                return r == modRank.end() ? kUnranked : r->second;
            };
            // winner (rank 0) merges LAST -> sort by rank DESCENDING; stable keeps within-mod Pandora order.
            std::stable_sort(all.begin(), all.end(),
                             [&](const std::string& a, const std::string& b) { return modRankOf(a) > modRankOf(b); });
            say("Pandora mode: applied user mod order (" + std::to_string(opt.pandoraModOrder.size()) +
                " mod(s) from the Pandora Order tab, winner last).");
        }

        bundleOrder = { kPandora };
        bundleCodes.clear();
        bundleCodes[kPandora] = std::move(all);
        say("Pandora mode: merging " + std::to_string(bundleCodes[kPandora].size()) +
            " code(s) into one Pandora.hky.");
        { std::string seq; for (const auto& c : bundleCodes[kPandora]) { if (!seq.empty()) seq += " "; seq += c; }
          say("Pandora mode: merge order (first loses, LAST wins): " + seq); }

        // Conflict pre-scan (Increment 2) — where merge order matters. Shared helper (also the UI's).
        const auto conflicts = ScanGraphConflicts(dataDir, bundleCodes[kPandora]);
        int nodeCollisions = 0; for (const auto& c : conflicts) nodeCollisions += c.nodeCollisions;
        say("Conflict scan: " + std::to_string(conflicts.size()) + " graph(s) touched by 2+ codes, " +
            std::to_string(nodeCollisions) + " base-node override collision(s) (order-sensitive).");
        for (const auto& c : conflicts) {
            std::string cs; for (const auto& x : c.codes) { if (!cs.empty()) cs += ", "; cs += x; }
            say("  " + c.graph + ": " + std::to_string(c.codes.size()) + " codes [" + cs + "]" +
                (c.nodeCollisions ? " — " + std::to_string(c.nodeCollisions) + " overridden node(s), winner '" +
                 c.winner + "'" : " — no node-override overlap"));
        }
    }

    auto codeDirOf = [&](const std::string& c) { return dataDir / "Nemesis_Engine" / "mod" / c; };

    // ── PatchPlan prelude (Phase 0 — behavior-neutral) ──────────────────────────────────────────
    // Normalize the raw Pandora/Nemesis load order into one authoritative plan (what changed, for
    // which actor/graph/character, resolved against the base archive). Nothing consumes it yet — this
    // dumps it to D:\cb-diffs\patchplan.txt so we can validate resolution (esp. horse -> actors/horse
    // + characters/horse.hkx) before Phase 1 flips the roster/filing legs onto it. See the plan file.
    if (baseArc) {
        std::unordered_map<std::string, std::vector<std::string>> precompiledByBundle;  // bundle -> loose-graph prefixes (lower)
        for (const auto& [prefix, mod] : prefixToMod) precompiledByBundle[mod].push_back(prefix);

        bconv::PlanInputs pin;
        pin.base                = &baseMaps;
        pin.graphs              = &graphs;
        pin.fpGraphs            = &fpGraphs;
        pin.bundleOrder         = &bundleOrder;
        pin.bundleCodes         = &bundleCodes;
        pin.precompiledByBundle = &precompiledByBundle;
        pin.patchDir = [&](const std::string& code, const std::string& g) -> std::string {
            std::error_code pe;
            const fs::path p = codeDirOf(code) / g;
            return fs::is_directory(p, pe) ? p.string() : std::string{};
        };
        pin.fpPatchDir = [&](const std::string& code, const std::string& g) -> std::string {
            std::error_code pe;
            const fs::path p = codeDirOf(code) / "_1stperson" / g;
            return fs::is_directory(p, pe) ? p.string() : std::string{};
        };
        const bconv::PatchPlan plan = bconv::BuildPlan(pin);
        bconv::DumpPlan(plan, "D:\\cb-diffs\\patchplan.txt");
        std::size_t graphCount = 0, rosterCount = 0;
        for (const auto& b : plan.bundles) { graphCount += b.graphs.size(); rosterCount += b.rosters.size(); }
        say("PatchPlan (Phase 0): " + std::to_string(plan.bundles.size()) + " bundle(s), " +
            std::to_string(graphCount) + " graph change(s), " + std::to_string(rosterCount) +
            " roster target(s), " + std::to_string(plan.warnings.size()) +
            " warning(s) — dumped to D:\\cb-diffs\\patchplan.txt (not yet consumed).");
    }

    for (const auto& bname : bundleOrder) {
        if (cancel) { r.error = "cancelled"; return r; }
        const std::vector<std::string>& codeList = bundleCodes[bname];
        const fs::path                  bundle   = plugins / (bname + ".hky");
        bool any = false;
        std::string codeLabel;   // "tdmh+tdmlen+tdmv" for logs
        for (const auto& c : codeList) { if (!codeLabel.empty()) codeLabel += "+"; codeLabel += c; }

        // 2a) behavior graph deltas — MERGE every owning code's patch dir for a graph in ONE convert.
        int n = 0;
        for (const auto& g : graphs) {
            std::vector<std::string> patchDirs;
            for (const auto& c : codeList) {
                const fs::path patch = codeDirOf(c) / g;
                if (fs::is_directory(patch, ec)) patchDirs.push_back(patch.string());
            }
            if (!patchDirs.empty() && convertBehaviorGraph(g, patchDirs, bname, unitOut(bname, g))) ++n;
        }

        // 2a') FIRST-PERSON behavior graph deltas — same cross-code merge against the first-person base.
        for (const auto& g : fpGraphs) {
            // `firstperson` is the FirstPerson CHARACTER (hkbCharacterData), NOT a behavior graph — its
            // patch (hkbCharacterStringData animationNames) is a ROSTER edit, handled by the set-data path
            // (LoadModAnimNames descends into _1stperson/). Converting it here as a behavior would mis-emit
            // it and drop the roster (the empty firstperson.txt / dropped-CRC bug).
            if (g == "firstperson") continue;
            std::vector<std::string> patchDirs;
            for (const auto& c : codeList) {
                const fs::path patch = codeDirOf(c) / "_1stperson" / g;
                if (fs::is_directory(patch, ec)) patchDirs.push_back(patch.string());
            }
            if (!patchDirs.empty() && convertFirstPersonGraph(g, patchDirs, bname, fpUnitOut(bname, g))) ++n;
        }

        if (n > 0) { any = true; say("  " + bname + " [" + codeLabel + "]: " + std::to_string(n) + " graph delta(s)."); }

        // 2b) set-data — MERGE every owning code's set-data dir in ONE call (ConvertNemesisSetData
        //     unions across modDirs in load order: crcs dedup, attacks delta-win, rosters accumulate).
        std::vector<fs::path> setDirs;
        for (const auto& c : codeList) {
            std::error_code sde;
            if (fs::is_directory(codeDirOf(c) / "animationsetdatasinglefile", sde)) setDirs.push_back(codeDirOf(c));
        }
        if (!setDirs.empty()) {
            const auto st = CommunityBehaviors::asd::ConvertNemesisSetData(
                setDirs, bundle, [&](const std::string& s) { say("    " + s); });
            r.skipped += static_cast<int>(st.fails);
            if (st.sets > 0 || st.charFiles > 0) {
                any = true; ++r.setMods;
                say("  " + bname + ": set-data — " + std::to_string(st.sets) + " set(s), " +
                    std::to_string(st.charFiles) + " roster(s).");
            }
        }

        // 2b') character roster deltas need no separate emission: ConvertNemesisSetData (2b)
        //     already wrote this mod's additions to bundle\animationnames\<char>.txt, and the
        //     runtime folds animationnames\ straight into the compiled character's roster
        //     (author-drop path) plus the set-data guard reads the same file. One form, two
        //     consumers — no redundant char-unit animations.txt mirror.

        // 2c) anim-data is emitted AFTER the RBG-chain ingest below (it needs every behaviour unit —
        //     incl. RBG-owned sub-behaviours — present so the clip-name -> animationName join is complete).
        //     See 2c' at the end of this per-mod block.

        // 2d) RBG-CHAIN INGEST — follow every RBG behaviorName in this mod's CHARACTER graph deltas
        //     out to the loose sub-behavior (DMCO's dodge chain 0_master -> DMCO.hkx -> DMCO_Dodge.hkx
        //     -> DMCO_Dodge1/2.hkx; SkyParkour's parkour graph), transitively, and do TWO things:
        //     (A) ROSTER (BR-14 safe cut): collect the chain's clip animationNames and emit them as
        //         animationnames\<char>.txt -> the runtime folds them into the compiled character
        //         roster (author-drop path) so the flattened clips bind (else char-setup can't bind
        //         the clip's animation -> null binding -> A-pose). Names come from the loose BINARY
        //         (literal strings) -> no XML-escape phantom-padding hazard (cf. UnescapeXml).
        //     (B) OWN (full cut): decompile each NEW sub-behavior into the bundle as an editable,
        //         SERVED YAML unit at its game-data path. The runtime serves any bundle unit
        //         (Resolver Owns/Resolve, keyed by the unit path) -> BR compiles+serves it in place
        //         of the loose file, so a BR-native mod can ship (and hand-edit) the sub-behaviors as
        //         YAML with no binary at all. This also subsumes BR-7 F1 (new standalone sub-behaviors
        //         were dropped in conversion). A VANILLA graph is skipped (owning a full standalone
        //         unit would shadow the base + other mods' deltas), as is any unit already present.
        //     behaviorName paths ("Behaviors\X.hkx") resolve under meshes\actors\character\.
        //     Round-trip is semantically faithful (YAML idempotent) but byte-different; the in-game
        //     gate covers the "engine stricter than the round-trip" risk (BR-12). The roster leg (A)
        //     stands alone, so if serving a recompiled sub-behavior regresses, dropping (B) still
        //     leaves the dodge fixed.
        {
            const fs::path                  charRoot = dataDir / "meshes" / "actors" / "character";
            std::unordered_set<std::string> visited;    // lowercased loose-file paths (cycle guard)
            // Vanilla graph stems stay a normal base+delta merge — never an owned standalone unit.
            std::unordered_set<std::string> vanillaStems;
            vanillaStems.reserve(graphs.size());
            for (const auto& g : graphs) vanillaStems.insert(ToLower(g));
            int ownedUnits = 0;

            // Per character-unit roster accumulation (serve path -> clip animationNames), routed by the
            // base maps' actor/character resolution. Replaces the old {defaultmale,defaultfemale} hardcode:
            // a horse graph's clips land in the horse character unit's data/animations.yaml, a first-person
            // graph's in firstperson, a humanoid graph's in defaultmale+defaultfemale — the roster the
            // schema reads natively (no bundle-root animationnames/ shim).
            std::map<std::string, std::vector<std::string>>        byServe;
            std::map<std::string, std::unordered_set<std::string>> seenByServe;
            auto addToServes = [&](const std::vector<std::string>& serves, const std::string& a) {
                if (a.empty()) return;
                for (const auto& s : serves)
                    if (!s.empty() && seenByServe[s].insert(ToLower(a)).second) byServe[s].push_back(a);
            };

            std::vector<std::string> curServes;   // the current seed graph's character serve path(s)
            std::function<void(const fs::path&)> walk = [&](const fs::path& hkx) {
                std::error_code we;
                if (!fs::is_regular_file(hkx, we)) return;
                if (!visited.insert(ToLower(hkx.lexically_normal().string())).second) return;
                LooseBehaviorRefs refs;
                if (!ReadLooseBehaviorRefs(hkx.string(), refs)) return;
                for (const auto& a : refs.animationNames) addToServes(curServes, a);      // (A) roster
                if (!vanillaStems.count(ToLower(hkx.stem().string()))) {                  // (B) own+serve
                    const fs::path  unitDir = bundle / hkx.lexically_normal().lexically_relative(dataDir.lexically_normal());
                    std::error_code ue;
                    if (!fs::exists(unitDir / "behavior.yaml", ue)) {
                        std::vector<std::uint8_t> ub;
                        std::string               uerr;
                        if (havok::sct::ReadHavokFile(hkx.string(), ub, &uerr)) {
                            const auto dres = havok::sct::DecompileToDir(ub, unitDir.string());
                            if (dres.ok && dres.kind == "behavior") ++ownedUnits;
                            else fs::remove_all(unitDir, ue);   // not a behavior / failed — leave no junk
                        }
                    }
                }
                for (const auto& b : refs.behaviorNames) {                               // recurse
                    std::string rel = b;
                    for (char& c : rel) if (c == '\\') c = '/';
                    walk(charRoot / rel);
                }
            };

            // Seed: every RBG behaviorName in this mod's just-written character graph deltas. The chain's
            // clips are routed to the SEED graph's character(s) (curServes), so a creature RBG chain lands
            // in its own actor's roster, not the humanoid one.
            for (const auto& g : graphs) {
                const fs::path  refsDir = fs::path(unitOut(bname, g)) / "references";
                std::error_code de;
                if (!fs::is_directory(refsDir, de)) continue;
                curServes = charServesFor(ToLower(g), /*firstPerson*/ false);
                for (const auto& e : fs::directory_iterator(refsDir, de)) {
                    std::ifstream f(e.path());
                    std::string   line;
                    while (std::getline(f, line)) {
                        const std::string bn = YamlQuoted(line, "behaviorName:");
                        if (bn.empty()) continue;
                        std::string rel = bn;
                        for (char& c : rel) if (c == '\\') c = '/';
                        walk(charRoot / rel);
                    }
                }
            }

            // DIRECT-GRAPH ROSTER — the RBG walk above only reaches sub-behaviors, but a mod's clips
            // also live DIRECTLY in the graphs it patches (BFCO_* in 1hm_behavior/bashbehavior). The
            // runtime graph-walk membrane used to collect these; do it here so the authored roster is
            // COMPLETE at convert time (the precondition for retiring that membrane). animationNames come
            // from the just-written delta unit clips/ (literal single-quoted strings), routed to the
            // patched graph's own character(s).
            {
                auto collectGraphClips = [&](const std::string& unitPath, const std::vector<std::string>& serves) {
                    if (serves.empty()) return;
                    const fs::path  clipsDir = fs::path(unitPath) / "clips";
                    std::error_code ce;
                    if (!fs::is_directory(clipsDir, ce)) return;
                    for (const auto& e : fs::directory_iterator(clipsDir, ce)) {
                        if (ToLower(e.path().extension().string()) != ".yaml") continue;
                        std::ifstream f(e.path());
                        std::string   line;
                        while (std::getline(f, line)) {
                            const std::string an = YamlQuoted(line, "animationName:");
                            if (!an.empty()) addToServes(serves, an);
                        }
                    }
                };
                for (const auto& g : graphs)   collectGraphClips(unitOut(bname, g),   charServesFor(ToLower(g), false));
                for (const auto& g : fpGraphs) if (g != "firstperson") collectGraphClips(fpUnitOut(bname, g), charServesFor(ToLower(g), true));
            }

            if (!byServe.empty()) {
                // Write each character unit's roster delta as data/animations.yaml — the runtime unions
                // every layer's yaml onto the base character (CharacterYamlLoader::LoadMerged), so a
                // collected name that IS vanilla is a harmless dedup there.
                int         totalAdded = 0;
                std::size_t totalNames = 0;
                for (const auto& [serve, names] : byServe) {
                    totalNames += names.size();
                    totalAdded += AddRosterToCharacterUnit(bundle, serve, names);
                }
                if (totalAdded > 0) {
                    any = true;
                    say("  " + bname + ": clip roster — " + std::to_string(totalNames) +
                        " clip animation(s) across " + std::to_string(byServe.size()) +
                        " character(s), " + std::to_string(totalAdded) + " new roster line(s).");
                }
            }
            if (ownedUnits > 0) {
                any = true;
                say("  " + bname + ": RBG-chain ingest — " + std::to_string(ownedUnits) +
                    " sub-behavior unit(s) decompiled to owned+served YAML (BR-native).");
            }
        }

        // 2c') anim-data — BR-native per-clip deltas: clips/<proj>/<clip>.yaml (animation: name, from the
        //      behaviour graph) + motion/<proj>/<clip>.yaml (real Nemesis motion). Runs here, after the
        //      RBG-chain ingest, so every behaviour unit (incl. RBG-owned) is present for the name join.
        //      The runtime resolves each clip's animIndex against the merged roster — no mod codes, no
        //      verbatim Nemesis copy, no high-band allocation.
        //      Per code into the shared bundle (clip-name-keyed: different codes add different clips;
        //      a same-named clip is last-writer, harmless). Accumulates across the bundle's codes.
        for (const auto& c : codeList) {
            if (const int clipFiles = DeriveModAnimDeltas(codeDirOf(c), bundle, dataDir, [&](const std::string& s){ say("    " + s); })) {
                any = true; ++r.animMods;
                say("  " + bname + " [" + c + "]: anim-data — " + std::to_string(clipFiles) +
                    " per-clip delta(s) (animation: + real motion).");
            }
        }

        // Emit the BUNDLE (mod) name — one slot per bundle (its codes already merged above).
        // A precompiled-only mod's bundle is added in the loose-replacement pass below.
        if (any) {
            if (std::find(emitted.begin(), emitted.end(), bname) == emitted.end()) emitted.push_back(bname);
            ++r.mods;
        }
    }

    // The master singlefiles (pristine vanilla animationdata/set-data, the base
    // ServeSetData/ServeAnimData name-gate on) are NOT emitted here — they ship inside the
    // Community Behaviors Skyrim.hky master (baked in by `--build-base`). `baseDir` is now unused
    // by this path; the mod bundles above carry only each mod's own set/anim-data DELTAS.
    (void)baseDir;

    // The synthetic loose-replacement bundle takes a load-order slot like any mod.
    if (charFilesAny) emitted.push_back("CharacterFiles");
    if (behFilesAny)  emitted.push_back("BehaviorFiles");
    // Attributed precompiled-graph bundles (pass 1d) also need a slot — but a mod that ALSO has a
    // Nemesis leg was already emitted above under the same <modName>, so dedup.
    for (const auto& b : precompiledBundles)
        if (std::find(emitted.begin(), emitted.end(), b) == emitted.end()) emitted.push_back(b);

    // 3b) FNIS — scan meshes/actors/<actor>/animations/ under the VFS for FNIS_*_List.txt
    //     files and emit one merged FNIS.hky bundle. Under MO2's VFS the animations/ folder
    //     is the union of every active mod's animation content, so a single scan sees all
    //     FNIS mods at once. Runs after set-data (which may carry FNIS-mod CRCs) and before
    //     loadorder.txt emission so FNIS.hky takes its slot.
    {
        if (cancel) { r.error = "cancelled"; return r; }
        const fs::path animDir = dataDir / "meshes" / "actors" / "character" / "animations";
        if (fs::is_directory(animDir, ec)) {
            say("");
            say("== FNIS ==");
            const fs::path fnisBundle = plugins / ((opt.singleBundle ? kPandora : std::string("FNIS")) + ".hky");
            auto fnisResult = CommunityBehaviors::fnis::ConvertFnis(
                { animDir }, "character", { "defaultmale", "defaultfemale" }, fnisBundle,
                [&](const std::string& s) { say("  " + s); });
            if (fnisResult.ok && fnisResult.animCount > 0) {
                r.fnisAnims  = static_cast<int>(fnisResult.animCount);
                r.fnisEvents = static_cast<int>(fnisResult.eventCount);
                if (!opt.singleBundle) emitted.push_back("FNIS");
                say("  FNIS.hky: " + std::to_string(fnisResult.animCount) + " anim(s), " +
                    std::to_string(fnisResult.eventCount) + " event(s), " +
                    std::to_string(fnisResult.varCount) + " var(s).");
            } else if (!fnisResult.ok) {
                ++r.skipped;
                say("  FNIS: FAILED — " + fnisResult.error);
            }
            for (const auto& w : fnisResult.warnings) say("    FNIS: " + w);
        }
    }

    // MO2-PROFILE: every leg routed into Pandora.hky, so the load order / manifests are just that one
    // bundle. Override the per-mod `emitted` list (which may have collected CharacterFiles/BehaviorFiles/
    // precompiled names) with the single Pandora bundle iff it actually got content.
    if (opt.singleBundle) {
        emitted.clear();
        if (fs::is_directory(plugins / (kPandora + ".hky"), ec)) emitted.push_back(kPandora);
    }

    // 3b) animationnames/ -> character-unit data/animations.yaml (Phase 1 retirement) — the set-data
    //     (2b) and FNIS legs still emit the legacy bundle-root animationnames/<stem>.txt roster drops.
    //     Re-file every remaining one into its character unit's data/animations.yaml (the native roster
    //     the schema reads via CharacterYamlLoader::LoadMerged), keyed by file stem -> base character
    //     serve path, then remove the shim folder so no bundle ships it. Roster lines are UnescapeXml'd
    //     to literal (data/animations.yaml holds literal paths; the shim drops may be XML-escaped, as the
    //     runtime read path unescaped them). A stem with no base character is left in place + warned.
    {
        std::error_code pe;
        int movedFiles = 0, movedNames = 0;
        for (fs::directory_iterator bi(plugins, pe), bEnd; !pe && bi != bEnd; bi.increment(pe)) {
            if (!bi->is_directory(pe)) continue;
            const fs::path anDir = bi->path() / "animationnames";
            if (!fs::is_directory(anDir, pe)) continue;
            bool anyUnroutable = false;
            for (fs::directory_iterator ai(anDir, pe), aEnd; !pe && ai != aEnd; ai.increment(pe)) {
                std::error_code fe;
                if (!ai->is_regular_file(fe)) continue;
                if (ToLower(ai->path().extension().string()) != ".txt") continue;
                const std::string stem = ToLower(ai->path().stem().string());
                const auto sit = charStemToServe.find(stem);
                if (sit == charStemToServe.end()) {
                    anyUnroutable = true;
                    say("  WARN: animationnames/" + ai->path().filename().string() + " in " +
                        bi->path().filename().string() + " has no base character '" + stem +
                        "' — left in place (unroutable).");
                    continue;
                }
                std::vector<std::string> names;
                if (std::ifstream in{ ai->path() }) {
                    std::string l;
                    while (std::getline(in, l)) {
                        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
                        const std::size_t s = l.find_first_not_of(" \t");
                        if (s == std::string::npos) continue;
                        const std::string t = l.substr(s);
                        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
                        names.push_back(UnescapeXml(t));
                    }
                }
                const int added = AddRosterToCharacterUnit(bi->path(), sit->second, names);
                if (added > 0) { ++movedFiles; movedNames += added; }
            }
            if (!anyUnroutable) fs::remove_all(anDir, pe);   // retire the shim folder from the bundle
        }
        if (movedFiles > 0)
            say("  roster: re-filed " + std::to_string(movedFiles) + " animationnames drop(s) into "
                "character units (" + std::to_string(movedNames) + " roster line(s)).");
    }

    // 4) loadorder.txt — PRESERVE an existing file's order (the user hand-tunes it and a
    //    regen must never clobber that: resetting it once reshuffled the merged event
    //    union under still-raw baked ids and crashed char-setup). Existing lines keep
    //    their order (even for codes that no longer contribute — harmless, user-owned);
    //    only genuinely NEW contributors are appended (lowest priority, user promotes).
    //    Fresh file: Skyrim (the master) first, then contributors. When discovery attributed
    //    bundles to mods, the NEW attributed appends are ORDERED BY MODLIST PRIORITY so the
    //    modlist winner (top of MO2's list, rank 0) lands LAST = highest BR priority (a later
    //    loadorder line overrides an earlier one, same as Skyrim-first). Un-attributed new
    //    bundles keep scan order and precede the attributed tail. Existing lines are never
    //    reordered (the hand-tuned-order invariant).
    {
        const fs::path loPath = outDir / "community_behaviors" / "loadorder.txt";
        std::vector<std::string> lines;
        std::unordered_set<std::string> have;   // lowercased entries (incl. comments' text — fine)
        auto lower = [](std::string s) {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        if (std::ifstream in{ loPath }) {
            std::string line;
            while (std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                lines.push_back(line);
                const auto b = line.find_first_not_of(" \t");
                if (b != std::string::npos && line[b] != '#' && line[b] != ';')
                    have.insert(lower(line.substr(b, line.find_last_not_of(" \t") - b + 1)));
            }
        }
        if (lines.empty()) lines.push_back("Skyrim"), have.insert("skyrim");
        else if (!have.count("skyrim")) { lines.insert(lines.begin(), "Skyrim"); have.insert("skyrim"); }
        // Gather genuinely-new bundles, then order them: un-attributed (scan order) first, then
        // attributed sorted by modlist rank DESCENDING (rank 0 / winner last = highest priority).
        std::vector<std::string> newUnknown, newKnown;
        for (const auto& b : emitted)
            if (have.insert(lower(b)).second) {
                (bundlePriority.count(b) ? newKnown : newUnknown).push_back(b);
            }
        std::stable_sort(newKnown.begin(), newKnown.end(),
                         [&](const std::string& a, const std::string& b) {
                             return bundlePriority.at(a) > bundlePriority.at(b);  // higher rank first, rank 0 last
                         });
        std::size_t appended = 0;
        for (const auto& b : newUnknown) { lines.push_back(b); ++appended; }
        for (const auto& b : newKnown)   { lines.push_back(b); ++appended; }
        std::ofstream lo(loPath, std::ios::binary);
        for (const auto& l : lines) lo << l << "\n";
        if (appended) say("  loadorder.txt: preserved existing order, appended " +
                          std::to_string(appended) + " new bundle(s).");
        else say("  loadorder.txt: preserved existing order (no new bundles).");
    }

    // 5) Manifests — one manifest.json per bundle (BundleManifest schema). Every mod bundle
    //    declares "Skyrim" as its master so BR can tell an override of a vanilla node from a
    //    net-new local state; the master itself (Skyrim.hky) ships with BR and carries its own
    //    manifest, so the converter never writes one (that would recreate a phantom Skyrim.hky
    //    here to collide with the shipped one). Per-mod identity is lifted from the Nemesis
    //    mod's info.ini when it ships one.
    say("");
    say("== Manifests ==");
    int manifests = 0;
    if (charFilesAny && !opt.singleBundle) {
        WriteManifest(plugins / "CharacterFiles.hky", "CharacterFiles", "", "", { "Skyrim" });
        ++manifests;
    }
    if (behFilesAny && !opt.singleBundle) {
        WriteManifest(plugins / "BehaviorFiles.hky", "BehaviorFiles", "", "", { "Skyrim" });
        ++manifests;
    }
    for (const auto& code : emitted) {
        if (code == "CharacterFiles" || code == "BehaviorFiles") continue;   // synthetic bundles handled above
        const NemesisInfo ni = ReadNemesisInfo(dataDir / "Nemesis_Engine" / "mod" / code);
        WriteManifest(plugins / (code + ".hky"),
                      ni.name.empty() ? code : ni.name,
                      ni.version, ni.author, { "Skyrim" });
        ++manifests;
    }
    say("  wrote " + std::to_string(manifests) + " manifest.json file(s).");

    // 6) Pack each per-mod bundle DIRECTORY into a single .hky FILE — mirroring how the master is
    //    packed by --build-base (ZipDir). A packed .hky is one archive the runtime reads via
    //    HkyArchive (MAX_PATH-immune, decompressed in memory), not a deep unpacked tree that MO2's
    //    VFS merges recursively. Collect first, then pack, so we don't mutate plugins\ mid-iterate.
    //    (Skyrim.hky ships separately with BR and is never present here; only *.hky DIRECTORIES are
    //    packed, so a stray packed file would be skipped.)
    say("");
    say("== Packing bundles ==");
    {
        std::vector<fs::path> bundleDirs;
        for (fs::directory_iterator bi(plugins, ec), bend; !ec && bi != bend; bi.increment(ec)) {
            std::error_code de;
            if (bi->is_directory(de) && ToLower(bi->path().extension().string()) == ".hky")
                bundleDirs.push_back(bi->path());
        }
        int packed = 0, packFail = 0;
        for (const auto& dir : bundleDirs) {
            const fs::path tmp = plugins / (dir.filename().string() + ".packing");
            std::error_code de;
            std::string     zerr;
            if (sct::util::ZipDir(dir.string(), tmp.string(), zerr)) {
                fs::remove_all(dir, de);
                fs::rename(tmp, dir, de);   // <code>.hky is now the packed archive file
                if (de) { say("  WARN: could not replace '" + dir.filename().string() + "': " + de.message()); ++packFail; }
                else    { ++packed; }
            } else {
                say("  WARN: failed to pack '" + dir.filename().string() + "': " + zerr + " (left unpacked)");
                fs::remove_all(tmp, de);
                ++packFail;
            }
        }
        say("  packed " + std::to_string(packed) + " bundle(s)" +
            (packFail ? (", " + std::to_string(packFail) + " failed") : "") + ".");
    }

    say("");
    say("Done (deltas over the shipped Skyrim.hky master): " + std::to_string(r.mods) +
        " mod bundle(s) (" + std::to_string(r.deltas) + " behavior delta(s), " +
        std::to_string(r.charDeltas) + " character roster delta(s), " +
        std::to_string(r.setMods) + " with set-data, " + std::to_string(r.animMods) +
        " with anim-data)" +
        (r.fnisAnims ? ("; FNIS: " + std::to_string(r.fnisAnims) + " anim(s)") : "") +
        (r.skipped ? ("; " + std::to_string(r.skipped) + " skipped") : "") + ".");
    fs::remove_all(baseBinTmp, ec);   // drop the per-graph base binaries compiled for pass 2a
    r.ok = true;
    return r;
}

// The Havok behavior-system kinds the master ingests. Determined BY CONTENT (the packfile's
// __classnames__ section — read from the file head), NOT by path/name. Path-matching which files
// are "corpus" was fragile: it silently dropped anything at an unexpected path (the first-person
// skeleton `characterassets/skeletonfirst.hkx`, and — the bet — others). The class-name markers are
// mutually exclusive at the root, so one cheap substring scan of the head classifies the file.
enum class HkxKind { Other, Skeleton, Behavior, Character, Project, Animation };
HkxKind PeekHkxKind(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return HkxKind::Other;
    std::string head(1u << 16, '\0');            // 64 KB — the __classnames__ section is at the file start
    f.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(f.gcount()));
    if (head.find("hkbProjectData")   != std::string::npos) return HkxKind::Project;
    if (head.find("hkbCharacterData") != std::string::npos) return HkxKind::Character;
    if (head.find("hkbBehaviorGraph") != std::string::npos) return HkxKind::Behavior;
    if (head.find("hkaSkeleton")      != std::string::npos) return HkxKind::Skeleton;
    // A loose animation carries hkaSplineCompressedAnimation (or interleaved) + hkaAnimationContainer
    // but NO hkaSkeleton (that lives in the skeleton file), so this can't collide with the skeleton
    // check above. Only spline animations are decompilable today — the round-trip pass skips the rest.
    if (head.find("hkaSplineCompressedAnimation")      != std::string::npos ||
        head.find("hkaInterleavedUncompressedAnimation") != std::string::npos) return HkxKind::Animation;
    return HkxKind::Other;                        // non-graph / non-animation asset
}

// ── Shared animation bake: decompile a spline animation to an ATTRIBUTED single-file unit at
// `unitPath`, self-gated on pose fidelity (round-trip rotation compare). Extracted verbatim from
// BuildBaseBundle so the master build AND per-mod conversion (ConvertLoadOrder) package animations
// through the ONE identical path — the precondition for serving mod animations natively. The emitted
// animation.yaml carries the full attributed unit (per-track bone, annotationTracks, motion) — see
// AnimationDef. `reason` receives a human note on Skip/Fail. Ok writes the yaml AS `unitPath` (the
// single-file unit collectNativeAnim consumes). `not spline-compressed` is a quiet Skip (Ok/Skip/Fail
// counting + logging stays the caller's, unchanged from the inline version).
enum class AnimBakeOutcome { Ok, Skip, Fail };
static AnimBakeOutcome BakeAnimationUnit(const std::vector<std::uint8_t>& abytes,
                                         const std::filesystem::path&     unitPath,
                                         const std::filesystem::path&     stageDir,
                                         std::string&                     reason,
                                         bool                             motionOnly = false) {
    namespace fs = std::filesystem;
    std::error_code ec;
    constexpr double kBakeMaxRotDeg = 0.5;   // character tree maxes 0.13deg after the codec fixes
    const fs::path bakeTmp = stageDir / "anim_bake";
    fs::remove_all(bakeTmp, ec); fs::create_directories(bakeTmp, ec);
    const auto dc = havok::anim::DecompileAnimation(abytes, bakeTmp);
    if (!dc.ok) { reason = dc.error; return AnimBakeOutcome::Skip; }   // incl. "not spline-compressed"

    // MOTION-ONLY package (point 5, compile-off bundles): keep the unit ONLY when it carries a `motion:`
    // block (AMR root motion translated at decompile — point 4), and SKIP the pose recompile+fidelity
    // gate. The pose is not served unless the bundle opts into compile (manifest compile_animations), so
    // pose fidelity is irrelevant here; we package the animation purely so its root motion feeds the adsf
    // (the runtime motionsForRoot read). A non-motion animation isn't packaged at all — no bloat from OAR
    // animation packs, only the motion-bearing clips.
    if (motionOnly) {
        std::ifstream in(bakeTmp / "animation.yaml", std::ios::binary);
        std::string   y((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (y.find("\n  motion:") == std::string::npos) { reason = "no root motion"; return AnimBakeOutcome::Skip; }
        fs::create_directories(unitPath.parent_path(), ec);
        std::ofstream dst(unitPath, std::ios::binary | std::ios::trunc);
        dst << y;
        return AnimBakeOutcome::Ok;
    }
    try {
        const auto ref = havok::anim::AnimationYamlLoader::Load(bakeTmp / "animation.yaml");
        const auto rc  = havok::anim::CompileAnimation(ref, 30);   // fps ignored: ref carries numFrames
        if (!rc.ok) { reason = "recompile: " + rc.error; return AnimBakeOutcome::Skip; }

        // pose gate: re-decompile the recompiled bytes and compare rotations to `ref`.
        const fs::path animRt = stageDir / "anim_rt"; fs::remove_all(animRt, ec); fs::create_directories(animRt, ec);
        if (!havok::anim::DecompileAnimation(rc.bytes, animRt).ok) { reason = "re-decompile"; return AnimBakeOutcome::Skip; }
        const auto rt = havok::anim::AnimationYamlLoader::Load(animRt / "animation.yaml");
        bool   faithful = ref.tracks.size() == rt.tracks.size();
        double maxDeg   = 0.0;
        for (std::size_t i = 0; faithful && i < ref.tracks.size(); ++i) {
            const auto& a = ref.tracks[i].rotation; const auto& b = rt.tracks[i].rotation;
            if (a.size() != b.size()) { faithful = false; break; }
            for (std::size_t k = 0; k < a.size(); ++k) {
                double dot = 0; for (int c = 0; c < 4; ++c) dot += (double)a[k].value[c] * b[k].value[c];
                dot = std::fabs(dot); if (dot > 1) dot = 1;
                maxDeg = std::max(maxDeg, 2.0 * std::acos(dot) * 57.2957795131);
            }
        }
        if (faithful && maxDeg <= kBakeMaxRotDeg) {
            fs::create_directories(unitPath.parent_path(), ec);
            std::ifstream src(bakeTmp / "animation.yaml", std::ios::binary);
            std::ofstream dst(unitPath, std::ios::binary | std::ios::trunc);
            dst << src.rdbuf();
            return AnimBakeOutcome::Ok;
        }
        reason = "fidelity " + std::to_string(maxDeg) + "deg";
        return AnimBakeOutcome::Skip;
    } catch (const std::exception& e) { reason = std::string("reload: ") + e.what(); return AnimBakeOutcome::Skip; }
}

BaseBuildResult BuildBaseBundle(const std::string& vanillaMeshesDir, const std::string& outHky,
                                const LogFn& log, const std::atomic<bool>& cancel,
                                const std::string& templatesDir, const std::string& keepStagingDir) {
    namespace fs = std::filesystem;
    const auto say = [&](const std::string& s) { if (log) log(s); };
    BaseBuildResult r;
    std::error_code ec;

    const fs::path meshes(vanillaMeshesDir);
    if (!fs::is_directory(meshes, ec)) { r.error = "vanilla meshes folder not found: " + vanillaMeshesDir; return r; }

    // The shared schema registry (havok::schema::SharedRegistry) must already be armed by the caller
    // (main.cpp points it at <exe>/Havok before --build-base / --regen-master) so the schema-native
    // animation round-trip pass can assemble + decompile. If it is not, the anim pass reports the
    // registry error per file and counts them as failures — it does not abort the corpus walk.

    // Stage under a SHORT root so the deep decompiled unit tree stays under MAX_PATH; only the packed
    // archive survives — UNLESS keepStagingDir is set, in which case the unpacked Skyrim.hky/ tree is
    // built there and KEPT (a short, observable layout with no unpack step; the caller picks the path).
    const bool     keepStage = !keepStagingDir.empty();
    const fs::path stage    = keepStage ? fs::path(keepStagingDir) : (fs::temp_directory_path(ec) / "sct_base_build");
    const fs::path stageHky = stage / "Skyrim.hky";
    // Baked native ANIMATIONS now ride in Skyrim.hky itself (point 1 of the root-motion/animation-compiler
    // plan — reunited with their behaviors/characters/projects). They are present in the master so their
    // ROOT MOTION can feed the adsf; whether they COMPILE/serve is gated per bundle by the manifest compile
    // flag (point 2), which Skyrim.hky omits. No separate "Skyrim - Animations.hky" bundle any more.
    fs::remove_all(stage, ec);
    fs::create_directories(stageHky / "meshes", ec);

    // NOTE: the singlefile decomposes are now FLAT per-project (keyed by the cache project-name stem),
    // landing in the two "<name>.txt/" folders — so the old project/character -> actor-root maps that
    // placed motion/movesets under actors/<actor>/ are gone (the folders don't need them).

    say("== Building Skyrim.hky master (full vanilla corpus) ==");
    for (fs::recursive_directory_iterator it(meshes, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (cancel) { r.error = "cancelled"; return r; }
        std::error_code fe;
        if (!it->is_regular_file(fe) || fe) continue;
        const std::string rel = fs::relative(it->path(), meshes, fe).generic_string();
        if (fe) continue;
        std::string lower = rel;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // FORMAT filter (extension only) then TYPE BY CONTENT — no path/name rules. PeekHkxKind reads the
        // file head's class table; a non-graph .hkx (animation = hkaAnimationContainer) is skipped. This
        // replaces the old path-matching `isCorpus`, which dropped anything at an unexpected path (e.g. the
        // first-person skeleton at `characterassets/` — and, the bet, more).
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".hkx") != 0) continue;
        const HkxKind kind = PeekHkxKind(it->path());
        if (kind == HkxKind::Other) continue;

        // Everything — behaviors, characters, projects, skeletons AND animations — bakes into the one base
        // bundle (Skyrim.hky). Animations reunited with the rest (point 1); the compile flag (point 2) decides
        // whether they serve.
        const fs::path unit = stageHky / "meshes" / fs::path(rel);

        // SKELETON: hkaSkeleton -> bonelist.yaml + bones/ unit (SkeletonImport -> EmitSkeletonYamlTree).
        // The runtime back-fills BehaviorData.boneNames from these (Resolver m_skeletons); without one,
        // every name-keyed hkbBoneIndexArray in that actor's graphs fails to compile.
        if (kind == HkxKind::Skeleton) {
            std::vector<std::uint8_t> sbytes; std::string srerr;
            std::vector<havok::skeleton::SkeletonData> sk; std::string skerr;
            if (havok::sct::ReadHavokFile(it->path().string(), sbytes, &srerr) &&
                havok::skeleton::LoadSkeletonsFromHkx(sbytes.data(), sbytes.size(), sk, &skerr) && !sk.empty()) {
                // Attach per-bone ragdoll physics (mass/radius/capsule/joint) BEFORE emit. Without this the
                // YAML tree is anim-only, CompileSkeletonFull derives 0 ragdoll bones, and BR's SERVED
                // skeleton has no ragdoll skeleton (skels[1]) → hkbRagdollDriver can't bind → every humanoid
                // bind-poses instead of ragdolling on death. Mirrors doSkeletonDecompileTree (the CLI verb);
                // this master-regen path is a SECOND skeleton HKX->YAML emitter and must round-trip the same.
                std::string perr;
                if (!havok::skeleton::ReadSkeletonPhysics(sbytes.data(), sbytes.size(), sk[0], &perr))
                    say("  note: skeleton physics read failed for " + rel + " (" + perr + ") — anim-only (no ragdoll derive).");
                std::string eerr;
                if (havok::skeleton::EmitSkeletonYamlTree(sk[0], unit, &eerr)) ++r.skeletons;
                else { ++r.failed; say("  skeleton emit FAILED: " + rel + " — " + eerr); }
            } else { ++r.failed; say("  skeleton read/parse FAILED: " + rel + " — " + (srerr.empty() ? skerr : srerr)); }
            continue;
        }

        // ANIMATION — BAKE the decompiled animation.yaml INTO the master, SELF-GATED on pose fidelity.
        // Decompile the loose spline animation to animation.yaml in its staged unit dir, then prove the
        // round-trip: recompile it, re-decompile that, and compare bone rotations frame-by-frame. Only if
        // the round-trip is faithful (exact frame count + every sample within kBakeMaxRotDeg) does the yaml
        // stay in the master; otherwise it's removed and the engine falls through to the loose vanilla .hkx.
        // So only animations CB can reproduce faithfully are served natively — the fidelity gate IS the
        // bake decision, per-animation, content-based (no path rules). Non-spline / undecodable clips skip.
        if (kind == HkxKind::Animation) {
            // A native animation is a SINGLE-FILE unit: the ".hkx" path IS a renamed animation.yaml (NOT a
            // "<name>.hkx/animation.yaml" tree). The decompile→pose-gate→write logic lives in the shared
            // BakeAnimationUnit so the master build and per-mod conversion package animations identically;
            // non-faithful/undecodable => no file written and the engine keeps the loose vanilla .hkx.
            std::vector<std::uint8_t> abytes; std::string arerr;
            if (!havok::sct::ReadHavokFile(it->path().string(), abytes, &arerr)) {
                ++r.animFail; say("  anim read FAILED: " + rel + " — " + arerr); continue;
            }
            std::string reason;
            switch (BakeAnimationUnit(abytes, unit, stage, reason)) {
                case AnimBakeOutcome::Ok:   ++r.animOk; break;
                case AnimBakeOutcome::Skip:
                    ++r.animSkip;
                    if (reason.find("not spline-compressed") == std::string::npos)   // quiet skip for those
                        say("  anim skip: " + rel + " — " + reason);
                    break;
                case AnimBakeOutcome::Fail: ++r.animFail; say("  anim FAILED: " + rel + " — " + reason); break;
            }
            continue;
        }

        // BEHAVIOR / CHARACTER / PROJECT — read full + decompile (DecompileToDir content-dispatches).
        std::vector<std::uint8_t> bytes;
        std::string rerr;
        if (!havok::sct::ReadHavokFile(it->path().string(), bytes, &rerr)) { ++r.failed; continue; }

        // A behavior we have a tagfile template XML for MUST be numbered by the oracle
        // (#NNNN), not DecompileToDir's encounter-order ids, so it merges against the
        // per-mod deltas (which reference those #NNNN). Route it through ConvertPatch's
        // vanilla-base path (no patch dirs). Fall back to DecompileToDir on any failure.
        //
        // STRUCTURAL GATE: a template is matched by stem, but stems collide — the 1st-person
        // `_1stperson/behaviors/1hm_behavior.hkx` shares the stem of the 3rd-person one yet is
        // a DIFFERENT graph. Applying the wrong template emits a garbage-numbered base (its
        // state refs dangle -> the runtime compile fails -> that actor T-poses). Only use the
        // template when its object count matches this binary's, i.e. it really describes THIS
        // file. A file under _1stperson/behaviors/ is numbered from templates/_1stperson/<stem>.xml (its
        // OWN first-person template, a different graph than the same-named third-person one) so the base's
        // #NNNN match the first-person deltas — the fix for the FirstPerson-project A-pose.
        if (kind == HkxKind::Behavior && !templatesDir.empty()) {
            std::error_code xe;
            // First-person graphs use their OWN templates/_1stperson/<stem>.xml (a different graph than the
            // same-named third-person one). This IS a legitimate path distinction (template selection), not
            // type detection — the `_1stperson` segment is how the two same-stem graphs are told apart.
            const bool is1st = lower.find("/_1stperson/") != std::string::npos;
            const fs::path xml = is1st
                ? fs::path(templatesDir) / "_1stperson" / (fs::path(rel).stem().string() + ".xml")
                : fs::path(templatesDir) / (fs::path(rel).stem().string() + ".xml");
            if (fs::exists(xml, xe)) {
                std::size_t binObjs = 0;
                try {
                    havok::PackFileDeserializer des;
                    havok::BinaryReaderEx rdr(false, true, bytes);
                    des.DeserializePartially(rdr);
                    binObjs = des.ListObjects().size();
                } catch (...) {}
                std::ifstream xf(xml, std::ios::binary);
                const std::string xtext((std::istreambuf_iterator<char>(xf)), std::istreambuf_iterator<char>());
                std::size_t xmlObjs = 0;
                for (std::size_t p = xtext.find("<hkobject name=\"#"); p != std::string::npos;
                     p = xtext.find("<hkobject name=\"#", p + 1)) ++xmlObjs;

                if (binObjs != 0 && binObjs == xmlObjs) {
                    // SCHEMA base decompile (the unified emitter): deserialize → SchemaObject → AssignIdentity
                    // (oracle #NNNN from this tagfile) → EmitHky + EmitFullBaseScaffolding. Byte-identical to
                    // the typed ConvertPatch/DecompileBehaviorTree (emit-check gate), but now the base master
                    // and the per-mod schema deltas are produced by ONE emitter — no base↔delta drift. Typed
                    // ConvertPatch stays as the fallback if the shared schema registry is unavailable or it errors.
                    if (auto* sreg = havok::schema::SharedRegistry()) {
                        std::string derr;
                        if (havok::model::DecompileBehaviorSchema(bytes, xtext, *sreg, unit.string(), derr)) {
                            ++r.behaviors; continue;
                        }
                        say("  WARN: schema base decompile failed for " + fs::path(rel).stem().string() +
                            " (" + derr + "); trying typed oracle.");
                    }
                    const auto pc = havok::sct::ConvertPatch(it->path().string(), xml.string(),
                                                             {}, "", "", unit.string());
                    if (pc.ok) { ++r.behaviors; continue; }
                    say("  WARN: oracle base decompile failed for " + fs::path(rel).stem().string() +
                        " (" + pc.error + "); using encounter-order ids.");
                } else if (xmlObjs != 0) {
                    say("  note: template " + fs::path(rel).stem().string() + ".xml (" +
                        std::to_string(xmlObjs) + " objs) != " + rel + " (" + std::to_string(binObjs) +
                        " objs) — different graph, using encounter-order ids.");
                }
            }
        }

        // No-template BEHAVIOR → schema READ-ORDER decompile (the coordinated flip): matches the schema
        // loose-derive so no-template mod deltas (horse, creatures) align with this base. Gated on the same
        // shared registry as the loose-derive, so base + delta are always both-schema or both-typed. Only
        // behaviors route here; character/project/animation stay on the typed DecompileToDir below.
        if (kind == HkxKind::Behavior) {
            if (havok::schema::SchemaRegistry* sreg = havok::schema::SharedRegistry()) {
                std::string derr;
                if (havok::model::DecompileBehaviorSchema(bytes, "", *sreg, unit.string(), derr)) { ++r.behaviors; continue; }
                say("  WARN: schema no-template decompile failed for " + rel + " (" + derr + "); typed fallback.");
            }
        }
        const auto d = havok::sct::DecompileToDir(bytes, unit.string());
        if (!d.ok) { ++r.failed; continue; }   // e.g. the 2 CC tagfile characters
        if      (d.kind == "behavior")  ++r.behaviors;
        else if (d.kind == "project")   ++r.projects;
        else if (d.kind == "character") ++r.characters;
    }
    say("  decompiled: " + std::to_string(r.behaviors) + " behaviors, " +
        std::to_string(r.projects) + " projects, " + std::to_string(r.characters) +
        " characters, " + std::to_string(r.skeletons) + " skeletons (" +
        std::to_string(r.failed) + " failed).");
    say("  animation round-trip: " + std::to_string(r.animOk) + " ok, " +
        std::to_string(r.animFail) + " failed, " + std::to_string(r.animSkip) +
        " skipped (non-spline / undecodable).");

    // NOTE: NEITHER singlefile .txt is copied into the master any more — both decompose into their
    // "<name>.txt/" FOLDER below (the winning architecture), and the anim-data / set-data servers
    // COMPOSE the base from those folders (byte-exact, gated by havok-core-cli {anim,set}data-tree-
    // roundtrip). animationsetdatasinglefile.txt/ = index + movesets(authored) + crcs(baked);
    // animationdatasinglefile.txt/ = index + clips(baked) + motion(authored).

    // Decompose the vanilla animationsetdata into its "animationsetdatasinglefile.txt/" FOLDER —
    // the same convention a "<name>.hkx" behavior decomposes into a "<name>.hkx/" folder. Three
    // pieces (see AnimSetDataYaml.h): index.yaml (project order + exact headers), movesets/<stem>.yaml
    // (AUTHORED gate/equip/attacks), crcs/<stem>.yaml (BAKED path-CRC residue, opaque). The folder IS
    // the base — the monolithic .txt no longer ships; the set-data server composes it back byte-exact
    // (gated: havok-core-cli setdata-tree-roundtrip). Flat per-project (keyed by header stem), so NO
    // actor-root resolution and NO noActor gap — every one of the 49 projects lands.
    say("== Skyrim editable caches (per-project YAML) ==");
    {
        std::ifstream sf(meshes / "animationsetdatasinglefile.txt", std::ios::binary);
        std::string   setText((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
        if (!setText.empty()) {
            try {
                const auto     parsed  = havok::animsetdata::ParseSingleFile(setText);
                const fs::path setRoot  = stageHky / "meshes" / "animationsetdatasinglefile.txt";
                fs::create_directories(setRoot / "movesets", ec);
                fs::create_directories(setRoot / "crcs", ec);

                // index.yaml — the project ORDER + exact header strings (load-bearing manifest).
                const std::string idx = havok::animsetdata::EmitSetdataIndexYaml(parsed);
                std::ofstream(setRoot / "index.yaml", std::ios::binary)
                    .write(idx.data(), static_cast<std::streamsize>(idx.size()));

                int movesets = 0, crcs = 0;
                for (const auto& proj : parsed.projects) {
                    const std::string stem = havok::animsetdata::StemForHeader(proj.header);
                    if (stem.empty()) continue;
                    if (!proj.sets.empty()) {
                        const std::string mv = havok::animsetdata::EmitMovesetsYaml(proj);
                        std::ofstream(setRoot / "movesets" / (stem + ".yaml"), std::ios::binary)
                            .write(mv.data(), static_cast<std::streamsize>(mv.size()));
                        ++movesets;
                        const std::string cr = havok::animsetdata::EmitSetdataCrcsYaml(proj);
                        std::ofstream(setRoot / "crcs" / (stem + ".yaml"), std::ios::binary)
                            .write(cr.data(), static_cast<std::streamsize>(cr.size()));
                        ++crcs;
                    }
                }
                say("  decomposed animationsetdatasinglefile.txt/ -> index.yaml + " +
                    std::to_string(movesets) + " movesets/ + " + std::to_string(crcs) +
                    " crcs/ (" + std::to_string(parsed.projects.size()) + " projects indexed)");
            } catch (const std::exception& e) {
                say(std::string("  WARN: animationsetdata decompose failed: ") + e.what());
            }
        }

        // Decompose the vanilla animationdata into its "animationdatasinglefile.txt/" FOLDER — the
        // sibling of the setdata folder above. Three pieces (see AnimDataYaml.h): index.yaml (the
        // per-project manifest: name + assets + hasAnimData, ALL 429 projects), clips/<stem>.yaml
        // (BAKED clip generators, until Part-B graph-derivation covers the base), motion/<stem>.yaml
        // (AUTHORED root motion, keyed by clip name + axis-labeled). The folder IS the base — the
        // monolithic .txt no longer ships; the anim-data server composes it back byte-exact (gated:
        // havok-core-cli animdata-tree-roundtrip). Flat per-project (keyed by project-name stem), so
        // NO actor-root resolution and NO noActor gap — the 380 header-only projects live entirely in
        // index.yaml, the 49 with anim data also get clips/ + motion/ bodies.
        std::ifstream af(meshes / "animationdatasinglefile.txt", std::ios::binary);
        std::string   animText((std::istreambuf_iterator<char>(af)), std::istreambuf_iterator<char>());
        if (!animText.empty()) {
            try {
                const auto     parsed  = havok::animdata::ParseSingleFile(animText);
                const fs::path animRoot = stageHky / "meshes" / "animationdatasinglefile.txt";
                fs::create_directories(animRoot / "clips", ec);
                fs::create_directories(animRoot / "motion", ec);

                // Resolve each project's character (roster source) by actor-folder co-location over the
                // just-decomposed stageHky tree, so clips are keyed by animation name (index resolved
                // from the roster at compile) instead of a hardwired magic number.
                const auto pcs = havok::animdata::LoadProjectCharacters((stageHky / "meshes").string());
                static const std::vector<std::string> kEmptyRoster;
                std::map<std::string, std::string> charRefByStem;
                for (const auto& [stem, pc] : pcs) charRefByStem.emplace(stem, pc.ref);

                // index.yaml — project manifest incl. the per-project `character:` ref.
                const std::string idx = havok::animdata::EmitAnimdataIndexYaml(parsed, charRefByStem);
                std::ofstream(animRoot / "index.yaml", std::ios::binary)
                    .write(idx.data(), static_cast<std::streamsize>(idx.size()));

                // Per-CLIP + per-MOTION files: clips/<stem>/<clipname>.yaml, motion/<stem>/<key>.yaml.
                // Clip name = filename (unique in project, FS-legal); motion keyed by its clip-label
                // (the clip at that animIndex) or "unnamed_<N>" for the hybrid blocks with no clip.
                int clips = 0, motion = 0, resolved = 0;
                for (const auto& proj : parsed.projects) {
                    if (!proj.hasAnimData) continue;
                    const std::string stem = havok::animdata::StemForProjectName(proj.name);
                    if (stem.empty()) continue;
                    const auto pit = pcs.find(stem);
                    const bool have = pit != pcs.end();
                    if (have) ++resolved;
                    const auto& roster = have ? pit->second.roster : kEmptyRoster;

                    const fs::path cdir = animRoot / "clips" / stem;
                    fs::create_directories(cdir, ec);
                    std::set<std::string> usedClip;
                    for (const auto& c : proj.clips) {
                        const std::string fn = havok::animdata::UniqueFileName(c.name, usedClip);
                        const std::string y  = havok::animdata::EmitClipYaml(c, roster, fn != c.name);
                        std::ofstream(cdir / (fn + ".yaml"), std::ios::binary)
                            .write(y.data(), static_cast<std::streamsize>(y.size()));
                        ++clips;
                    }
                    if (!proj.motions.empty()) {
                        // idx -> clip-name label (the motion file key; "unnamed_<N>" when no clip).
                        long maxIdx = -1;
                        for (const auto& g : proj.clips)   maxIdx = std::max(maxIdx, std::atol(g.animIndex.c_str()));
                        for (const auto& m : proj.motions) maxIdx = std::max(maxIdx, std::atol(m.animIndex.c_str()));
                        std::vector<std::string> labels(static_cast<std::size_t>(maxIdx + 1));
                        for (const auto& g : proj.clips) {
                            const long i = std::atol(g.animIndex.c_str());
                            if (i >= 0 && labels[static_cast<std::size_t>(i)].empty()) labels[static_cast<std::size_t>(i)] = g.name;
                        }
                        auto normKey = [](std::string s){ for (char& c : s) c = (char)std::tolower((unsigned char)c);
                                                          while (!s.empty() && (s.back()=='.'||s.back()==' ')) s.pop_back(); return s; };
                        const fs::path mdir = animRoot / "motion" / stem;
                        fs::create_directories(mdir, ec);
                        std::set<std::string> usedMot;
                        for (const auto& m : proj.motions) {
                            const long i = std::atol(m.animIndex.c_str());
                            std::string key = (i >= 0 && static_cast<std::size_t>(i) < labels.size() && !labels[static_cast<std::size_t>(i)].empty())
                                              ? labels[static_cast<std::size_t>(i)] : ("unnamed_" + m.animIndex);
                            // A label that collides case-insensitively falls back to the always-unique
                            // "unnamed_<animIndex>" (which the compose resolves by raw index, no label).
                            if (!usedMot.insert(normKey(key)).second) { key = "unnamed_" + m.animIndex; usedMot.insert(normKey(key)); }
                            const std::string y = havok::animdata::EmitMotionSidecar(m);
                            std::ofstream(mdir / (key + ".yaml"), std::ios::binary)
                                .write(y.data(), static_cast<std::streamsize>(y.size()));
                            ++motion;
                        }
                    }
                }
                say("  decomposed animationdatasinglefile.txt/ -> index.yaml + " +
                    std::to_string(clips) + " clip file(s) + " + std::to_string(motion) +
                    " motion file(s) (" + std::to_string(parsed.projects.size()) + " projects, " +
                    std::to_string(resolved) + " char-resolved)");

                // ── MOTION REUNION ────────────────────────────────────────────────────────────
                // Bethesda stripped root motion from each animation and duplicated it onto every clip
                // that referenced it in the adsf. Put it back: for each project clip that resolves to an
                // animation (via the roster), take that clip's motion record and append it onto the baked
                // animation.yaml unit as a top-level `motion:` block — so the animation carries its own
                // motion again. Keyed by the resolved animation UNIT PATH (CanonicalAnimPath: actor root +
                // roster entry), so it can't collide across actors; duplicates across male/female are
                // byte-identical, so first-writer wins. The runtime re-duplicates per clip at name-index
                // resolution (collect-by-clip) — this step just makes each animation self-describing.
                // Unnamed/hybrid motion (no unique roster slot) is left index-keyed in the adsf as before.
                {
                    auto lc = [](std::string s){ for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
                    std::size_t reunited = 0, unbaked = 0;
                    for (const auto& proj : parsed.projects) {
                        if (!proj.hasAnimData || proj.motions.empty()) continue;
                        const std::string stem = havok::animdata::StemForProjectName(proj.name);
                        const auto pit = pcs.find(stem);
                        if (pit == pcs.end()) continue;                     // unresolved -> clips kept raw index
                        const auto& roster = pit->second.roster;
                        const std::string refl = lc(pit->second.ref);
                        const auto cp = refl.find("/characters/");
                        if (cp == std::string::npos) continue;              // can't locate actor root
                        const std::string actorRoot = refl.substr(0, cp);   // "actors/<...>"
                        for (const auto& m : proj.motions) {
                            char* end = nullptr; const long i = std::strtol(m.animIndex.c_str(), &end, 10);
                            if (!(end && *end == '\0' && i >= 0 && (std::size_t)i < roster.size())) continue;
                            if (roster[(std::size_t)i].empty()) continue;
                            const std::string canon = havok::animdata::CanonicalAnimPath(actorRoot, roster[(std::size_t)i]);
                            // native animation is a SINGLE-FILE unit: the ".hkx" path IS the yaml (not a
                            // "<name>.hkx/animation.yaml" tree). Append the motion block onto that file.
                            const fs::path animYaml = stageHky / "meshes" / canon;   // baked anims now live in Skyrim.hky
                            std::error_code fe;
                            if (!fs::exists(animYaml, fe)) { ++unbaked; continue; }   // not baked -> vanilla loose keeps its adsf motion
                            // don't double-append (first-writer wins across the male/female duplicate).
                            std::ifstream chk(animYaml, std::ios::binary);
                            std::string cur((std::istreambuf_iterator<char>(chk)), std::istreambuf_iterator<char>());
                            chk.close();
                            if (cur.find("\n  motion:") != std::string::npos) continue;
                            // append `motion:` as a child of `animation:` — EmitMotionSidecar body indented +4.
                            std::string body = havok::animdata::EmitMotionSidecar(m), blk = "  motion:\n";
                            for (std::size_t p = 0; p < body.size();) {
                                std::size_t nl = body.find('\n', p);
                                std::string line = body.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
                                if (!line.empty()) blk += "    " + line + "\n";
                                p = (nl == std::string::npos) ? body.size() : nl + 1;
                            }
                            std::ofstream(animYaml, std::ios::binary | std::ios::app) << blk;
                            ++reunited;
                        }
                    }
                    say("  motion reunion: " + std::to_string(reunited) + " animation(s) reunited with their motion record (" +
                        std::to_string(unbaked) + " referenced-but-unbaked, left to vanilla loose).");
                }
            } catch (const std::exception& e) {
                say(std::string("  WARN: animationdata decompose failed: ") + e.what());
            }
        }
    }

    say("  packing -> " + outHky + " ...");
    std::string zerr;
    if (!sct::util::ZipDir(stageHky.string(), outHky, zerr)) { r.error = "pack failed: " + zerr; return r; }

    // Animations are now IN Skyrim.hky (no separate bundle). Remove any stale sibling "Skyrim -
    // Animations.hky" left by an older build so the runtime doesn't load its animations twice.
    {
        std::error_code de;
        const fs::path staleAnim = fs::path(outHky).parent_path() / "Skyrim - Animations.hky";
        if (fs::remove(staleAnim, de)) say("  removed stale " + staleAnim.string() + " (animations now in Skyrim.hky).");
    }

    if (keepStage) say("  kept unpacked tree: " + stageHky.string());
    else           fs::remove_all(stage, ec);   // keep only the archives
    say("  master built: " + outHky);
    r.ok = true;
    return r;
}

// Per-mod animation packaging (piece 1) — see the forward declaration above ConvertLoadOrder. Walks each
// enabled mod's loose meshes\ for animation .hkx and bakes them into that mod's <modName>.hky bundle as
// attributed single-file units (the per-mod sibling of BuildBaseBundle's animation leg). Index-bound for
// now; bone-name binding (the membrane) is piece 2. FAIL-SAFE: a skip/fail writes no unit and the engine
// keeps the loose .hkx, so this can never break the behavior/skeleton bundles.
static void PackageModAnimations(const Mo2Layout& mo2, const fs::path& plugins,
                                 const fs::path& stageRoot, const LogFn& log,
                                 const std::string& targetBundle) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    std::error_code ec;
    const fs::path stage = stageRoot / "modanim_stage";
    fs::create_directories(stage, ec);

    // In single-bundle (Pandora) mode ALL mods' animations land in the one `targetBundle` (Pandora.hky);
    // otherwise each mod's animations go in its own <modName>.hky. Root-motion packaging (point 5):
    // BakeAnimationUnit runs MOTION-ONLY — it keeps only the clips that carry AMR root motion (translated
    // to the `motion:` field at decompile), so the runtime adsf-derive (motionsForRoot) picks up their
    // root motion. Non-motion animations (plain OAR replacers) are not packaged. Compile/serve of the
    // pose stays gated by the bundle's manifest compile_animations flag (default false).
    int totalOk = 0, totalSkip = 0, totalFail = 0, modsWithAnims = 0;
    for (const std::string& modName : mo2.enabledTopFirst) {
        const fs::path modRoot = mo2.modsDir / modName;
        const fs::path meshes  = modRoot / "meshes";
        if (!fs::is_directory(meshes, ec)) continue;
        const fs::path bundle = plugins / ((targetBundle.empty() ? modName : targetBundle) + ".hky");
        int modOk = 0, modSkip = 0, modFail = 0;
        for (fs::recursive_directory_iterator it(meshes, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path& p = it->path();
            const std::string lower = ToLower(p.filename().string());
            if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".hkx") != 0) continue;
            if (PeekHkxKind(p) != HkxKind::Animation) continue;

            std::vector<std::uint8_t> abytes; std::string rerr;
            if (!havok::sct::ReadHavokFile(p.string(), abytes, &rerr)) { ++modFail; continue; }
            const fs::path rel = fs::relative(p, modRoot, ec);    // "meshes/actors/.../<name>.hkx"
            if (ec || rel.empty()) { ++modFail; continue; }
            const fs::path unit = bundle / rel;                   // single-file attributed unit inside the bundle
            std::string reason;
            switch (BakeAnimationUnit(abytes, unit, stage, reason, /*motionOnly=*/true)) {
                case AnimBakeOutcome::Ok:   ++modOk;   break;
                case AnimBakeOutcome::Skip: ++modSkip; break;   // no root motion — not packaged (harmless)
                case AnimBakeOutcome::Fail: ++modFail; break;
            }
        }
        if (modOk > 0) {
            ++modsWithAnims;
            say("  mod root-motion animations: " + modName + " -> " + std::to_string(modOk) + " motion unit(s).");
        }
        totalOk += modOk; totalSkip += modSkip; totalFail += modFail;
    }
    fs::remove_all(stage, ec);
    if (totalOk || totalSkip || totalFail)
        say("Mod animations: " + std::to_string(totalOk) + " root-motion unit(s) across " +
            std::to_string(modsWithAnims) + " mod(s) (" + std::to_string(totalSkip) + " no-motion/skipped, " +
            std::to_string(totalFail) + " failed).");
}

RegenResult RegenerateMaster(const std::string& vanillaMeshesDir, const std::string& templatesDir,
                             const std::string& outHky, bool strict,
                             const LogFn& log, const std::atomic<bool>& cancel,
                             const std::string& keepStagingDir) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    RegenResult r;
    std::error_code ec;

    const fs::path work    = fs::temp_directory_path(ec) / "sct_regen_master";
    const fs::path tempHky = work / "Skyrim.hky";
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    // 1) Build the whole master to a TEMP file (never touch outHky until it validates). keepStagingDir
    //    (optional) keeps the unpacked Skyrim.hky/ tree there for observation — built regardless of the
    //    validation outcome below (you want to inspect it precisely when it DIDN'T validate).
    r.build = BuildBaseBundle(vanillaMeshesDir, tempHky.string(), log, cancel, templatesDir, keepStagingDir);
    if (!r.build.ok) { r.error = "build-base failed: " + r.build.error; fs::remove_all(work, ec); return r; }

    // 2) Fidelity gate — for every templated graph, compile its base unit out of the fresh master,
    //    decompile it, and byte-diff data/graphdata.yaml against the SAME graph's vanilla binary
    //    decompile. graphdata carries the symbol tables + role/flags the XML-oracle path used to
    //    corrupt (37 vs 5804/5849), so a drift here is the exact alarm we lacked.
    say("");
    say("== Validating regenerated master vs vanilla (graphdata fidelity) ==");
    std::string herr;
    auto arc = havok::model::HkyArchive::LoadFromFile(tempHky.string(), herr);
    if (!arc) { r.error = "cannot reopen built master for validation: " + herr; fs::remove_all(work, ec); return r; }

    const fs::path gate = work / ".gate";
    auto readFile = [](const fs::path& p) -> std::string {
        std::ifstream f(p, std::ios::binary);
        return f ? std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()) : std::string{};
    };

    // Skeleton back-fill for the gate compile: bone-index arrays in the base units are stored by NAME
    // (e.g. 206_bones), so CompileBehavior needs the actor's bone list to resolve them — exactly as the
    // runtime does (Resolver back-fills BehaviorData.boneNames from the per-actor skeleton). The gate is
    // character-family (actors/character/behaviors/*, incl. horsebehavior), so load the ONE character
    // skeleton once. Absent it, name-keyed graphs (0_master/horsebehavior) just stay un-gated (as before).
    std::vector<std::string> charBoneNames;
    {
        const fs::path skel = fs::path(vanillaMeshesDir) / "actors" / "character" / "character assets" / "skeleton.hkx";
        std::vector<std::uint8_t> sb; std::string serr;
        if (havok::sct::ReadHavokFile(skel.string(), sb, &serr)) {
            std::vector<havok::skeleton::SkeletonData> sk;
            if (havok::skeleton::LoadSkeletonsFromHkx(sb.data(), sb.size(), sk, &serr) && !sk.empty())
                for (const auto& b : sk[0].bones) charBoneNames.push_back(b.name);
        }
        say(charBoneNames.empty()
                ? "  note: character skeleton not loaded (" + skel.string() + ") — name-keyed bone arrays can't be gate-compiled"
                : "  gate: character skeleton loaded (" + std::to_string(charBoneNames.size()) + " bones) for bone-name resolution");
    }

    for (const auto& g : ScanGraphs(templatesDir)) {
        if (cancel) { r.error = "cancelled"; fs::remove_all(work, ec); return r; }
        const fs::path van = fs::path(vanillaMeshesDir) / "actors" / "character" / "behaviors" / (g + ".hkx");
        if (!fs::is_regular_file(van, ec)) continue;   // only gate graphs we have vanilla ground truth for
        const std::string prefix = std::string(kBehSub) + "/" + g + ".hkx";

        std::vector<std::uint8_t> reBytes;              // regenerated base unit -> binary
        try {
            auto data = havok::model::YamlBehaviorLoader::LoadMerged({ arc->source(prefix) });
            if (data.boneNames.empty()) data.boneNames = charBoneNames;   // back-fill skeleton (like the runtime)
            const auto cr = havok::sct::CompileBehavior(data);
            if (!cr.ok) { say("  " + g + ": regen base compile failed — " + cr.error); continue; }
            reBytes = cr.bytes;
        } catch (const std::exception& e) { say("  " + g + ": regen base load threw — " + std::string(e.what())); continue; }

        std::vector<std::uint8_t> vanBytes; std::string rerr;
        if (!havok::sct::ReadHavokFile(van.string(), vanBytes, &rerr)) { say("  " + g + ": vanilla read failed — " + rerr); continue; }

        const fs::path reDir = gate / (g + "_re"), vaDir = gate / (g + "_va");
        const auto rd = havok::sct::DecompileToDir(reBytes,  reDir.string());
        const auto vd = havok::sct::DecompileToDir(vanBytes, vaDir.string());
        if (rd.ok && vd.ok) {
            ++r.checked;
            if (readFile(reDir / "data" / "graphdata.yaml") == readFile(vaDir / "data" / "graphdata.yaml"))
                ++r.faithful;
            else { r.drift.push_back(g); say("  DRIFT: " + g + " — graphdata differs from vanilla"); }
        } else {
            say("  " + g + ": decompile failed (regen:" + (rd.ok ? "ok" : rd.error) + " van:" + (vd.ok ? "ok" : vd.error) + ")");
        }
        fs::remove_all(reDir, ec); fs::remove_all(vaDir, ec);
    }
    std::string driftList;
    for (const auto& d : r.drift) { if (!driftList.empty()) driftList += ", "; driftList += d; }
    say("  fidelity: " + std::to_string(r.faithful) + "/" + std::to_string(r.checked) +
        " templated graph(s) match vanilla graphdata" + (r.drift.empty() ? "." : ("; drift: " + driftList)));

    // 3) Promote — or, in strict mode, refuse to write if anything drifted.
    if (!r.drift.empty() && strict) {
        r.error = "strict gate: " + std::to_string(r.drift.size()) +
                  " graph(s) drift from vanilla graphdata (" + driftList + ") — master NOT written";
        say("  " + r.error);
        fs::remove_all(work, ec);
        return r;
    }
    fs::create_directories(fs::path(outHky).parent_path(), ec);
    fs::copy_file(tempHky, outHky, fs::copy_options::overwrite_existing, ec);
    if (ec) { r.error = "failed to write master to " + outHky + ": " + ec.message(); fs::remove_all(work, ec); return r; }
    // Animations are now baked into Skyrim.hky (point 1). Remove any stale sibling "Skyrim -
    // Animations.hky" at the output dir so the runtime never loads its animations twice.
    {
        std::error_code ae;
        const fs::path staleAnim = fs::path(outHky).parent_path() / "Skyrim - Animations.hky";
        if (fs::remove(staleAnim, ae)) say("  removed stale " + staleAnim.string() + " (animations now in Skyrim.hky).");
    }
    fs::remove_all(work, ec);
    say("  master -> " + outHky + (r.drift.empty() ? "" : "  (written with " + std::to_string(r.drift.size()) +
        " known/benign drift graph(s) — review the DRIFT lines above)"));
    r.ok = true;
    return r;
}

}  // namespace bconv
