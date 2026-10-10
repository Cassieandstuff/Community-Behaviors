#include "Converter.h"

#include <codec/serialization/HavokFile.h>             // ReadHavokFile / WriteHavokFile
#include <codec/formid/FormId.h>                        // CB::core::codec::formid::SELF_INDEX / BASE_GAME_INDEX (index convention)
#include <decompile/SkeletonImport.h>        // LoadSkeletonsFromHkx / ReadSkeletonPhysics (skeleton import)
#include <codec/format/SkeletonYaml.h>       // EmitSkeletonYamlTree — SkeletonData -> bonelist.yaml + bones/ unit
#include <codec/serialization/HkyArchive.h>     // Skyrim.hky base = the loose-derive vanilla source
#include <codec/format/AnimationSetData.h>     // parse vanilla animationsetdatasinglefile
#include <codec/format/AnimSetDataYaml.h>      // EmitMovesetsYaml (vanilla decompose)
#include <interface/AnimationData.h>        // parse vanilla animationdatasinglefile
#include <codec/format/AnimDataYaml.h>         // EmitMotionYaml (motion decompose)
#include <codec/serialization/packfile/PackFileDeserializer.h> // object-count gate for template matching + ConstructAllOfClass (pass 2d)
#include <codec/serialization/packfile/BinaryReaderEx.h>       // BinaryReaderEx — drives ConstructAllOfClass
#include <compile/AnimationCompiler.h>       // CB::core::anim::CompileAnimation — recompile leg (schema-native)
#include <decompile/AnimationDecompiler.h>   // CB::core::anim::DecompileAnimation — schema-native import leg
#include <codec/motion/RootMotion.h>
#include <codec/serialization/MetaText.h>
#include <compile/AnimDataProjection.h>
#include <compile/AnimDataFromBehavior.h>

#include <atomic>
#include <process.h>   // _getpid — a private per-process staging root
#include <mutex>
#include <thread>
#include <codec/format/AnimationYamlLoader.h> // AnimationYamlLoader::Load — animation.yaml -> AnimationDef
#include <decompile/BehaviorDecompile.h>     // ConvertModDelta / DeriveHkyPatch / the reflective unit codec
#include <interface/reflection/HavokSchema.h>        // SchemaRegistry (Havok/ class descriptors)
#include <codec/serialization/HavokIo.h>     // io::MakeSchemaFactory / io::SchemaObject (schema decompile dispatch)
#include "BaseUnitCompiler.h"               // rig-bound base-unit compile (regen gates)
#include "NemesisSetDataConvert.h"   // CommunityBehaviors::asd::ConvertNemesisSetData (shared with br-nemesis-to-hky)
#include "FnisConverter.h"           // CommunityBehaviors::fnis::ConvertFnis (shared with br-fnis-to-hky)
#include "PatchPlan.h"               // BuildBaseMaps / BuildPlan / DumpPlan — the Pandora normalization prelude (Phase 0)
#include "platform/SctUtilities.h"       // ZipDir — pack the staged master into one Skyrim.hky
#include <codec/bsa/BsaArchive.h>

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
#include <sstream>
#include <string_view>
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

// Read a string field off a generic SchemaObject by name (empty if absent / not a string field).
std::string SoStr(const CB::core::codec::io::SchemaObject& so, const char* name) {
    const auto flds = so.Fields();
    const auto& vs = so.Values();
    for (std::size_t i = 0; i < flds.size() && i < vs.size(); ++i)
        if (flds[i]->name == name) return vs[i].str;
    return {};
}


// Decode XML entity references to their literal characters. The character DECOMPILER emits roster paths through an
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
    if (!CB::core::codec::ReadHavokFile(hkxPath, bytes, &err)) return false;
    // Schema-native: build the clip/RBG objects as generic SchemaObjects (schema factory) and read the
    // one field each off the field table by name — no typed hkbClipGenerator/hkbBehaviorReferenceGenerator.
    CB::core::schema::SchemaRegistry* reg = CB::core::schema::SharedRegistry();
    if (!reg) return false;
    try {
        CB::core::codec::PackFileDeserializer des;
        des.ObjectFactory = CB::core::codec::io::MakeSchemaFactory(*reg);
        CB::core::codec::BinaryReaderEx       br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        des.DeserializePartially(br);
        const bool le = des._header.Endian == 0;
        const bool p8 = des._header.PointerSize == 8;
        {
            CB::core::codec::BinaryReaderEx dr(le, p8, des.DataSectionBytes());
            for (const auto& o : des.ConstructAllOfClass(dr, "hkbClipGenerator"))
                if (const auto* c = dynamic_cast<const CB::core::codec::io::SchemaObject*>(o.get())) {
                    const std::string an = SoStr(*c, "animationName");
                    if (!an.empty()) out.animationNames.push_back(an);
                }
        }
        {
            CB::core::codec::BinaryReaderEx dr(le, p8, des.DataSectionBytes());
            for (const auto& o : des.ConstructAllOfClass(dr, "hkbBehaviorReferenceGenerator"))
                if (const auto* r = dynamic_cast<const CB::core::codec::io::SchemaObject*>(o.get())) {
                    const std::string bn = SoStr(*r, "behaviorName");
                    if (!bn.empty()) out.behaviorNames.push_back(bn);
                }
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
// key). Each is a character unit in Skyrim.hky at meshes/<relative path>/, which the Resolver
// serves with mod roster patches merged — the compile-time replacement for the runtime
// animationNames injection. Returns meshes-relative paths ("actors/.../x.hkx").
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

/// @brief Read a packfile through the schema and hand its first `cls` object to `fn` (while the graph is alive).
/// @return false when the bytes don't deserialize or hold no `cls`.
static bool WithFirstOfClass(const std::vector<std::uint8_t>& bytes, const CB::core::schema::SchemaRegistry& reg,
                             std::string_view cls, const std::function<void(CB::core::codec::io::SchemaObject&)>& fn) {
    try {
        CB::core::codec::PackFileDeserializer des;
        des.ObjectFactory = CB::core::codec::io::MakeSchemaFactory(reg);
        CB::core::codec::BinaryReaderEx br(false, true, bytes);
        des.Deserialize(br);
        for (const auto& [off, o] : des.DeserializedObjects())
            if (auto* so = dynamic_cast<CB::core::codec::io::SchemaObject*>(o.get()); so && cls == so->ClassName()) {
                fn(*so);
                return true;
            }
    } catch (const std::exception&) {}
    return false;
}

/// @brief Character roster additions, per (bundle dir, character serve path), flushed as character PATCH units
///        (FlushRosters) once every leg has contributed.
/// @note  Names dedup case-insensitively: a name differing only by case would pad the roster and shift the
///        animation-binding indices (the phantom-padding hazard).
struct RosterSink {
    std::map<std::pair<std::string, std::string>, std::vector<std::string>>        names;
    std::map<std::pair<std::string, std::string>, std::unordered_set<std::string>> seen;
};

/// @return how many of `names` are new to the sink for that character.
int AddRosterToCharacterUnit(RosterSink& sink, const fs::path& bundleDir, const std::string& characterServePath,
                             const std::vector<std::string>& names) {
    if (names.empty() || characterServePath.empty()) return 0;
    const std::pair<std::string, std::string> key{ bundleDir.string(), ToLower(characterServePath) };
    auto& have = sink.seen[key];
    auto& list = sink.names[key];
    int added = 0;
    for (const auto& a : names)
        if (!a.empty() && have.insert(ToLower(a)).second) { list.push_back(a); ++added; }
    return added;
}

/// @brief Write the sink as each bundle's character PATCHES: a union roster stating only the names the base lacks.
/// @warning Characters sharing a root behavior (defaultmale + defaultfemale) bind their shared graphs' clips
///          POSITIONALLY, so their rosters must stay slot-aligned: the engine keys a slot by its lowercase FILENAME, and
///          `male\MT_X` in a clip IS the female roster's `female\MT_X`. So a name whose filename any member's base
///          already holds is vanilla, and every genuine addition goes to EVERY member, in the same order. Deduping per
///          character appended 17 phantom `male\MT_*` to defaultfemale, shifting every later slot (BFCO / SkyParkour
///          clips bound the wrong animation — or past the roster into OAR's appended range: `1hm_equip`).
/// @return the number of character patch units written.
int FlushRosters(const RosterSink& sink, bconv::BaseUnitCompiler& base, const CB::core::codec::HkyArchive& arc,
                 const CB::core::schema::SchemaRegistry& reg, const LogFn& log) {
    const auto say = [&](const std::string& m) { if (log) log(m); };
    const auto fileKey = [](const std::string& n) {
        const auto p = n.find_last_of("\\/");
        return ToLower(p == std::string::npos ? n : n.substr(p + 1));
    };
    std::map<std::string, std::string>              groupOf;      // character serve key -> alignment group
    std::map<std::string, std::vector<std::string>> members;      // group -> character serve keys
    std::map<std::string, std::vector<std::string>> baseRoster;   // character serve key -> its base animationNames
    for (const auto& u : arc.units()) {
        if (u.kind != CB::core::codec::HkyArchive::UnitKind::Character) continue;
        const auto src = arc.source(u.prefix);
        const auto cs  = src ? CB::core::decompile::ReadCharacterStrings(*src) : std::nullopt;
        if (!cs) continue;
        const std::string serve = ToLower(u.prefix);
        const auto s1 = serve.find_last_of('/');
        const auto s2 = s1 == std::string::npos || s1 == 0 ? std::string::npos : serve.find_last_of('/', s1 - 1);
        std::string behavior = ToLower(cs->behaviorFilename);
        for (char& c : behavior) if (c == '\\') c = '/';
        const std::string group = (s2 == std::string::npos ? serve : serve.substr(0, s2)) + "|" + behavior;
        groupOf[serve] = group;
        members[group].push_back(serve);
        baseRoster[serve] = cs->animationNames;
    }

    std::map<std::pair<std::string, std::string>, std::vector<std::string>> additions;   // (bundle, group) -> names
    for (const auto& [key, names] : sink.names) {
        const auto& [bundleDir, serve] = key;
        const auto g = groupOf.find(serve);
        if (g == groupOf.end()) { say("  roster: " + serve + " — no base character; additions dropped."); continue; }
        auto& list = additions[{ bundleDir, g->second }];
        list.insert(list.end(), names.begin(), names.end());
    }

    int written = 0;
    for (const auto& [key, names] : additions) {
        const auto& [bundleDir, group] = key;
        std::unordered_set<std::string> have;
        for (const auto& m : members[group])
            for (const auto& n : baseRoster[m]) have.insert(fileKey(n));
        std::vector<std::string> add;
        for (const auto& n : names) if (!n.empty() && have.insert(fileKey(n)).second) add.push_back(n);
        if (add.empty()) continue;
        for (const auto& serve : members[group]) {
            CB::core::decompile::ParsedHky g, fresh;
            std::string err;
            std::vector<std::uint8_t> bytes;
            const auto& bones = base.BonesFor(serve);
            const fs::path unit = fs::path(bundleDir) / fs::path(serve);
            bool ok = base.Parse(serve, g, err);
            if (ok)
                CB::core::decompile::GraphEdit(g, reg).ForEach("hkbCharacterStringData", [&](CB::core::codec::io::SchemaObject& so) {
                    auto& roster = so.FieldRef("animationNames").strs;
                    roster.insert(roster.end(), add.begin(), add.end());
                });
            ok = ok && CB::core::decompile::SerializeGraph(g, bytes, err) && base.Parse(serve, fresh, err) &&
                 CB::core::decompile::DeriveHkyPatch(fresh, { "skyrim" }, { ToLower(fs::path(bundleDir).stem().string()), "skyrim" },
                                                     bytes, reg, unit.string(), err, bones.empty() ? nullptr : &bones);
            if (!ok) { say("  roster: " + serve + " — patch FAILED: " + err); continue; }
            ++written;
        }
    }
    return written;
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
    fs::path                 profileDir;
    std::vector<std::string> enabledTopFirst;
    /// @brief Active plugins (original file names) in LOAD ORDER, earliest first.
    /// @note Active = starred in plugins.txt, OR present only in loadorder.txt: MO2 never lists the implicit
    ///   base-game/Creation Club plugins in plugins.txt because the engine always loads them.
    std::vector<std::string> activePluginsInOrder;
    /// @brief Directory of the EFFECTIVE Skyrim.ini (+ SkyrimCustom.ini): the profile when MO2's
    ///   LocalSettings=true, else Documents\My Games\<game>. Empty if none was found.
    fs::path                 iniDir;
};

/// @brief Trim trailing CR/LF/spaces (MO2 profile files are CRLF).
std::string TrimLineEnd(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

/// @brief Fill Mo2Layout::activePluginsInOrder from the profile's loadorder.txt + plugins.txt.
void ReadActivePlugins(Mo2Layout& out) {
    std::unordered_map<std::string, bool> listed;   ///< lower(name) -> starred, for every plugins.txt entry
    std::vector<std::string>              pluginsOrder;
    if (std::ifstream pl{ out.profileDir / "plugins.txt" }) {
        std::string line;
        while (std::getline(pl, line)) {
            line = TrimLineEnd(line);
            if (line.empty() || line[0] == '#') continue;
            const bool active = line[0] == '*';
            const std::string name = active ? line.substr(1) : line;
            listed[ToLower(name)] = active;
            if (active) pluginsOrder.push_back(name);
        }
    }
    std::ifstream lo{ out.profileDir / "loadorder.txt" };
    if (!lo) { out.activePluginsInOrder = std::move(pluginsOrder); return; }
    std::string line;
    while (std::getline(lo, line)) {
        line = TrimLineEnd(line);
        if (line.empty() || line[0] == '#') continue;
        const auto it = listed.find(ToLower(line));
        if (it == listed.end() || it->second) out.activePluginsInOrder.push_back(line);
    }
}

/// @brief Resolve Mo2Layout::iniDir: the profile when its settings.ini has LocalSettings=true, else the first
///   Documents\My Games folder (SSE, then GOG) that holds a Skyrim.ini.
void ResolveIniDir(Mo2Layout& out) {
    std::error_code ec;
    if (std::ifstream s{ out.profileDir / "settings.ini" }) {
        std::string line;
        while (std::getline(s, line))
            if (ToLower(TrimLineEnd(line)) == "localsettings=true") { out.iniDir = out.profileDir; return; }
    }
    const std::string docsUtf8 = sct::util::DocumentsFolder();
    if (docsUtf8.empty()) return;
    const fs::path docs(std::u8string(docsUtf8.begin(), docsUtf8.end()));
    for (const char* game : { "Skyrim Special Edition", "Skyrim Special Edition GOG" }) {
        const fs::path d = docs / "My Games" / game;
        if (fs::is_regular_file(d / "Skyrim.ini", ec)) { out.iniDir = d; return; }
    }
}

/// @brief The [Archive] sResourceArchiveList + sResourceArchiveList2 entries, in order.
/// @note SkyrimCustom.ini overrides any key it sets (the engine's INI layering); names compare case-insensitively.
std::vector<std::string> ReadIniArchiveLists(const fs::path& iniDir) {
    std::unordered_map<std::string, std::string> keys;   ///< lower(key) -> raw value
    for (const char* file : { "Skyrim.ini", "SkyrimCustom.ini" }) {
        std::ifstream in(iniDir / file);
        std::string line, section;
        while (std::getline(in, line)) {
            line = TrimLineEnd(line);
            if (!line.empty() && line.front() == '[') { section = ToLower(line); continue; }
            if (section != "[archive]") continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = ToLower(line.substr(0, eq));
            if (key == "sresourcearchivelist" || key == "sresourcearchivelist2") keys[key] = line.substr(eq + 1);
        }
    }
    std::vector<std::string> out;
    for (const char* key : { "sresourcearchivelist", "sresourcearchivelist2" }) {
        const auto it = keys.find(key);
        if (it == keys.end()) continue;
        std::stringstream ss(it->second);
        std::string item;
        while (std::getline(ss, item, ',')) {
            while (!item.empty() && item.front() == ' ') item.erase(item.begin());
            while (!item.empty() && item.back() == ' ') item.pop_back();
            if (!item.empty()) out.push_back(item);
        }
    }
    return out;
}
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
    out.profileDir = profs / profile;
    ReadActivePlugins(out);
    ResolveIniDir(out);
    return out;
}

/// @brief Decide which of the enabled mods' root .bsa files the ENGINE actually loads, with MO2 VFS precedence.
/// @note The engine loads archives three ways (verified in Ghidra, AE 1.6.1170):
///   1. the INI lists sResourceArchiveList + sResourceArchiveList2 (loaded first => lowest precedence);
///   2. engine built-ins: MarketplaceTextures.bsa, opened unconditionally by the marketplace/UI init (FUN_14154f4e0);
///   3. per active .esp/.esm/.esl, EXACTLY "<stem>.bsa" and "<stem> - Textures.bsa" (LoadPluginBSA @ 1401bf710),
///      case-insensitive. Any other plugin-named suffix (e.g. "Foo - Animations.bsa") is never loaded.
///   Every name resolves through the VFS, where the highest-priority mod providing that archive name wins.
/// @return every root archive of every enabled mod with a counted/skipped verdict + reason; counted ones sorted by
///   precedence (INI/built-in first at loadIndex -1, then plugin load order; loose files beat all archives).
std::vector<ModArchiveInfo> ResolveModArchivesFor(const Mo2Layout& mo2) {
    std::unordered_map<std::string, std::string> engineListed;   ///< lower(archive name) -> why the engine loads it
    if (!mo2.iniDir.empty())
        for (const auto& name : ReadIniArchiveLists(mo2.iniDir))
            engineListed.emplace(ToLower(name), "listed in " + (mo2.iniDir / "Skyrim.ini").string() + " [Archive]");
    engineListed.emplace("marketplacetextures.bsa", "engine built-in (opened unconditionally at marketplace/UI init)");

    std::unordered_map<std::string, int> loadIndex;   ///< lower(plugin stem) -> load index
    for (int i = 0; i < static_cast<int>(mo2.activePluginsInOrder.size()); ++i) {
        const fs::path p(mo2.activePluginsInOrder[i]);
        const std::string ext = ToLower(p.extension().string());
        if (ext == ".esp" || ext == ".esm" || ext == ".esl") loadIndex[ToLower(p.stem().string())] = i;
    }

    std::vector<ModArchiveInfo>                  out;
    std::unordered_map<std::string, std::string> winner;   ///< lower(archive file name) -> owning (highest-priority) mod
    std::error_code ec;
    for (const std::string& modName : mo2.enabledTopFirst) {
        const fs::path root = mo2.modsDir / modName;
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path& p = it->path();
            if (ToLower(p.extension().string()) != ".bsa") continue;
            ModArchiveInfo a;
            a.modName = modName;
            a.archive = p.string();
            const std::string key = ToLower(p.filename().string());
            if (const auto w = winner.find(key); w != winner.end()) {
                a.reason = "overridden by higher-priority mod '" + w->second + "'";
                out.push_back(std::move(a));
                continue;
            }
            winner.emplace(key, modName);

            if (const auto el = engineListed.find(key); el != engineListed.end()) {
                a.counted = true;
                a.reason = el->second;
                out.push_back(std::move(a));
                continue;
            }
            std::string stem = ToLower(p.stem().string());
            static const std::string kTex = " - textures";
            if (stem.size() > kTex.size() && stem.compare(stem.size() - kTex.size(), kTex.size(), kTex) == 0)
                stem.resize(stem.size() - kTex.size());
            if (const auto li = loadIndex.find(stem); li != loadIndex.end()) {
                a.counted = true;
                a.plugin = mo2.activePluginsInOrder[li->second];
                a.loadIndex = li->second;
                a.reason = "loaded by active plugin '" + a.plugin + "'";
            } else if (const auto dash = stem.rfind(" - "); dash != std::string::npos && loadIndex.count(stem.substr(0, dash))) {
                a.reason = "suffix '" + stem.substr(dash) + "' is never auto-loaded (only <stem>.bsa and <stem> - Textures.bsa)";
            } else {
                a.reason = "no active plugin named '" + stem + "'";
            }
            out.push_back(std::move(a));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const ModArchiveInfo& a, const ModArchiveInfo& b) {
        if (a.counted != b.counted) return a.counted;
        return a.counted && a.loadIndex < b.loadIndex;
    });
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

}  // namespace  (file-local helpers above; the plan API below is external in bconv)

// ── Pre-conversion load-order plan (the framework the GUI arranger + the converter read) ─────────────
// Parse a SPECIFIC profile's modlist.txt (top-first enabled) — the "point straight at the profile" path.
static Mo2Layout ReadProfileLayout(const fs::path& profileDir, const fs::path& modsDir) {
    Mo2Layout out;
    std::ifstream ml(profileDir / "modlist.txt");
    if (!ml) return out;
    std::string line;
    while (std::getline(ml, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] != '+') continue;   // '-' disabled, '#'/'*' separators
        out.enabledTopFirst.push_back(line.substr(1));
    }
    out.ok         = !out.enabledTopFirst.empty();
    out.modsDir    = modsDir;
    out.profileDir = profileDir;
    ReadActivePlugins(out);
    ResolveIniDir(out);
    return out;
}

std::vector<ModArchiveInfo> ResolveModArchives(const std::string& mo2Instance, const LogFn& log) {
    const Mo2Layout mo2 = ResolveMo2Layout(mo2Instance);
    if (!mo2.ok) {
        if (log) log("not an MO2 instance (no profiles/mods, or an empty modlist): " + mo2Instance);
        return {};
    }
    if (log) log("MO2 profile: " + mo2.profileDir.string() + " — " + std::to_string(mo2.enabledTopFirst.size()) +
                 " enabled mod(s), " + std::to_string(mo2.activePluginsInOrder.size()) + " active plugin(s).");
    if (log) log(mo2.iniDir.empty() ? std::string("INI archive lists: no Skyrim.ini found (INI-listed archives not counted)")
                                    : "INI archive lists: " + mo2.iniDir.string() + " (" +
                                          std::to_string(ReadIniArchiveLists(mo2.iniDir).size()) + " listed)");
    return ResolveModArchivesFor(mo2);
}

// Scan one mod's Nemesis patch dirs for the DISTINCT mod-codes it REFERENCES ("#<code>$<N>" markers) —
// how a bundle's cross-bundle masters are discovered (a code it references but doesn't own is foreign).
static std::set<std::string> ScanReferencedCodesForMod(const fs::path& modDir, const std::vector<std::string>& ownCodes) {
    std::set<std::string> refs;
    std::error_code se;
    for (const auto& c : ownCodes) {
        const fs::path codeDir = modDir / "Nemesis_Engine" / "mod" / c;
        for (fs::recursive_directory_iterator it(codeDir, se), end; !se && it != end; it.increment(se)) {
            if (!it->is_regular_file()) continue;
            const std::string fn = it->path().filename().string();
            if (fn.empty() || fn[0] != '#' || it->path().extension() != ".txt") continue;
            std::ifstream in(it->path(), std::ios::binary);
            const std::string t((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (std::size_t p = t.find('#'); p != std::string::npos; p = t.find('#', p + 1)) {
                std::size_t i = p + 1, dollar = std::string::npos;
                for (; i < t.size(); ++i) {
                    const char ch = t[i];
                    if (ch == '$') dollar = i;
                    else if (!(std::isalnum((unsigned char)ch) || ch == '_')) break;
                }
                if (dollar != std::string::npos && dollar > p + 1 && i > dollar + 1)
                    refs.insert(ToLower(t.substr(p + 1, dollar - (p + 1))));
            }
        }
    }
    return refs;
}

LoadOrderPlan BuildLoadOrderPlan(const std::string& profileOrInstance, const LogFn& log) {
    LoadOrderPlan plan;
    std::error_code ec;
    const fs::path in = profileOrInstance;

    // Resolve the layout: a profile dir (has modlist.txt) is used verbatim; an instance root resolves its
    // active profile. This is the profile-first entry the redesigned GUI drives.
    fs::path instanceRoot;
    Mo2Layout mo2;
    if (fs::is_regular_file(in / "modlist.txt", ec)) {                 // pointed straight at a profile
        instanceRoot   = in.parent_path().parent_path();              // profiles/<profile> -> instance
        mo2            = ReadProfileLayout(in, instanceRoot / "mods");
        plan.profileDir = in.string();
    } else if (fs::is_directory(in / "mods", ec) && fs::is_directory(in / "profiles", ec)) {
        instanceRoot = in;
        mo2          = ResolveMo2Layout(in);                          // active profile
    } else {
        plan.error = "not an MO2 profile or instance: " + profileOrInstance;
        return plan;
    }
    if (!mo2.ok) { plan.error = "no enabled modlist under " + profileOrInstance; return plan; }
    plan.modsDir = mo2.modsDir.string();

    // Resolve the DATA dir the converter reads the base + existing bundles from: it needs
    // <dataDir>/community_behaviors/plugins/Skyrim.hky (the master everything derives against). That base
    // ships INSIDE the Community Behaviors mod, so under a standalone (non-VFS) run it is NOT in the game
    // Data — find the enabled mod (top-first) that ships it and use ITS folder as the data dir. When the
    // tool is launched THROUGH MO2 the VFS ./Data already overlays that mod, but resolving the mod folder
    // works either way. Fall back to the ini gamePath\Data only if no mod ships the base.
    {
        std::error_code de;
        for (const std::string& modName : mo2.enabledTopFirst) {
            const fs::path cand = mo2.modsDir / modName;
            if (fs::is_regular_file(cand / "community_behaviors" / "plugins" / "Skyrim.hky", de)) {
                plan.gameDataDir = cand.generic_string();   // the folder that IS the base's data root
                break;
            }
        }
    }
    // gamePath from ModOrganizer.ini — a fallback data root (and surfaced in the GUI) when no mod ships the base.
    if (plan.gameDataDir.empty())
        if (std::ifstream ini{ instanceRoot / "ModOrganizer.ini" }) {
            std::string line;
            while (std::getline(ini, line)) {
                const auto p = line.find("gamePath");
                if (p == std::string::npos) continue;
                const auto eq = line.find('=', p); if (eq == std::string::npos) break;
                std::string v = line.substr(eq + 1);
                if (const auto ba = v.find("ByteArray("); ba != std::string::npos) {
                    const auto o = v.find('(', ba), c = v.rfind(')');
                    if (o != std::string::npos && c != std::string::npos && c > o) v = v.substr(o + 1, c - o - 1);
                }
                while (!v.empty() && (v.back()=='\r'||v.back()=='\n'||v.back()==' '||v.back()=='"')) v.pop_back();
                while (!v.empty() && (v.front()==' '||v.front()=='"')) v.erase(v.begin());
                for (char& ch : v) if (ch == '\\') ch = '/';
                if (!v.empty()) plan.gameDataDir = v + "/Data";
                break;
            }
        }

    std::unordered_set<std::string> excluded;   // engine codes (Pandora/Nemesis) dropped by discovery
    const auto contribs = DiscoverContributions(mo2, log, excluded);

    // code -> owning mod = the DEDICATED mod (fewest codes; tie -> top load priority). A shared code
    // (e.g. `dmco` in both BFCO and the standalone Dodge MCO-DXP) is owned by the mod it is the primary
    // content of, so it lives in ONE bundle and other mods MASTER it — matching the converter's grouping.
    {
        std::vector<const LooseModContribution*> byDedication;
        for (const auto& c : contribs) byDedication.push_back(&c);
        std::stable_sort(byDedication.begin(), byDedication.end(),
            [](const LooseModContribution* a, const LooseModContribution* b) {
                if (a->nemesisCodes.size() != b->nemesisCodes.size()) return a->nemesisCodes.size() < b->nemesisCodes.size();
                return a->priority < b->priority;
            });
        for (const LooseModContribution* c : byDedication)
            for (const auto& code : c->nemesisCodes)
                plan.codeOwner.emplace(ToLower(code), c->modName);
    }

    // Per-mod: master table = [Skyrim, ...cross-bundle owners of the FOREIGN codes it references...],
    // ordered by load priority (winner first). Self is the implicit index 0.
    std::unordered_map<std::string, int> priOf;
    for (const auto& c : contribs) priOf[c.modName] = c.priority;
    for (const auto& c : contribs) {
        PlanMod pm;
        pm.modName        = c.modName;
        pm.priority       = c.priority + 1;               // 1 = winner (top)
        pm.codes          = c.nemesisCodes;
        pm.hasPrecompiled = !c.precompiledGraphs.empty();
        pm.masters.push_back("Skyrim");                   // base, present for all (index 1)

        std::unordered_set<std::string> own;
        for (const auto& code : c.nemesisCodes) own.insert(ToLower(code));
        std::set<std::string> foreignMods;
        // (1) SHARED-OWNED codes: a code this mod ships is CANONICALLY owned by a higher-priority mod
        // (both ship it — e.g. BFCO bundles `dmco`, Dodge MCO-DXP owns `dmco`). The lower-priority mod
        // masters the owner so its edits to that code's nodes resolve to the owner's identity, not a
        // separate self node. This is the case that fixed the DMCO_Stt Dodge collision.
        for (const auto& oc : own)
            if (const auto it = plan.codeOwner.find(oc); it != plan.codeOwner.end() && it->second != c.modName)
                foreignMods.insert(it->second);
        // (2) REFERENCED foreign codes: a `#code$N` this mod's patches reference that another mod owns.
        const auto refs = ScanReferencedCodesForMod(mo2.modsDir / c.modName, c.nemesisCodes);
        for (const auto& rc : refs)
            if (!own.count(rc))
                if (const auto it = plan.codeOwner.find(rc); it != plan.codeOwner.end() && it->second != c.modName)
                    foreignMods.insert(it->second);
        std::vector<std::string> ordered(foreignMods.begin(), foreignMods.end());
        std::stable_sort(ordered.begin(), ordered.end(), [&](const std::string& a, const std::string& b) {
            const int pa = priOf.count(a) ? priOf[a] : 1<<30, pb = priOf.count(b) ? priOf[b] : 1<<30;
            return pa != pb ? pa < pb : a < b;
        });
        for (auto& m : ordered) pm.masters.push_back(m);
        if (log && ordered.size())
            log("  plan: '" + c.modName + "' masters " + std::to_string(ordered.size()) +
                " cross-bundle bundle(s) via shared code(s).");
        plan.mods.push_back(std::move(pm));
    }
    plan.ok = true;
    return plan;
}

std::string EmitLoadOrderText(const LoadOrderPlan& plan) {
    // One mod per line, winner (priority 1) FIRST, with its resolved master table. The authoritative,
    // reproducible record of the order the conversion used. Lines are "prio\tmod\t[codes]\tmasters:...".
    std::string out = "# Community Behaviors load order (winner first)\n";
    out += "# self = FormId index 0; masters listed are indices 1..k (Skyrim first)\n";
    for (const auto& m : plan.mods) {
        out += std::to_string(m.priority) + "\t" + m.modName + "\t[";
        for (std::size_t i = 0; i < m.codes.size(); ++i) { if (i) out += ","; out += m.codes[i]; }
        out += "]\tmasters:";
        for (std::size_t i = 0; i < m.masters.size(); ++i) { if (i) out += ","; out += m.masters[i]; }
        out += "\n";
    }
    return out;
}

namespace {  // file-local helpers resume

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
    Mo2Layout     mo2;   // hoisted to function scope so the per-mod animation pass (after `plugins` below) can walk enabled mods
    LoadOrderPlan plan;  // the SAME pre-conversion plan the GUI arranger builds — single source of truth for the
                         // load order + resolved master tables; logged here and exported as the authoritative record.
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
                // Pre-conversion plan: decide the load order + cross-bundle master tables ONCE, up-front,
                // so they're stable and reproducible. The GUI arranger builds this same object; exported
                // below as loadorder.plan.txt (the runtime loadorder.txt — bundle order — is written later).
                plan = BuildLoadOrderPlan(inst.string(), log);
                if (plan.ok)
                    say("Plan: " + std::to_string(plan.mods.size()) + " behavior mod(s), master tables resolved (self=0, Skyrim=1).");
                const auto contribs = DiscoverContributions(mo2, log, excludedCodes);
                for (const auto& c : contribs) bundlePriority[c.modName] = c.priority;
                // OWNERSHIP of a shared code = the DEDICATED mod. A code carried by several mods (e.g. `dmco`
                // in both BFCO's integration and the standalone Dodge MCO-DXP) is owned by the mod for which
                // it is the primary content — the one with the FEWEST codes (tie -> top load priority). This
                // keeps a shared code in ONE bundle (its dedicated mod) and lets the bundling mod MASTER it,
                // instead of top-priority absorbing it into a multi-code bundle where its locals collide with
                // that bundle's own code (the bfco$3 / dmco$3 -> 0000x0003 collision).
                {
                    std::vector<const LooseModContribution*> byDedication;
                    for (const auto& c : contribs) byDedication.push_back(&c);
                    std::stable_sort(byDedication.begin(), byDedication.end(),
                        [](const LooseModContribution* a, const LooseModContribution* b) {
                            if (a->nemesisCodes.size() != b->nemesisCodes.size())
                                return a->nemesisCodes.size() < b->nemesisCodes.size();   // fewest codes = dedicated
                            return a->priority < b->priority;                              // tie -> top priority
                        });
                    for (const LooseModContribution* c : byDedication)
                        for (const auto& code : c->nemesisCodes)
                            codeToMod.emplace(ToLower(code), c->modName);   // dedicated owner claims first
                }
                for (const auto& c : contribs) {
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

    CB::core::schema::SchemaRegistry* sharedReg = CB::core::schema::SharedRegistry();
    if (!sharedReg) { r.error = "schema registry not armed (" + CB::core::schema::SharedRegistryError() + ")"; return r; }
    const CB::core::schema::SchemaRegistry& schemaReg = *sharedReg;
    RosterSink rosters;

    const fs::path plugins = outDir / "community_behaviors" / "plugins";
    // MO2-PROFILE (single-bundle) mode ships one merged bundle; every leg routes into it. (Definition
    // hoisted here so the loose-derive / char-file legs above the bundle grouping can use it too.)
    const std::string kPandora = "Pandora";

    if (mo2.ok) PackageModAnimations(mo2, plugins, outDir, log, opt.singleBundle ? kPandora : std::string());
    fs::create_directories(plugins, ec);

    // A graph's REAL meshes-relative serve path (base-map ground truth) — bound after baseMaps loads
    // (below), called from the per-bundle loop (later still). Fixes the humanoid hardcode: a Nemesis
    // patch against a non-humanoid templated graph (horsebehavior) files its delta under its own actor
    // (actors/horse) instead of actors/character, so the runtime actually applies it. kBehSub[1st]
    // fallback for a new/custom graph not present in the base.
    std::function<std::string(const std::string& g, bool firstPerson)> graphServePathOf;

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

    /// @note The shipped Skyrim.hky base is REQUIRED: every patch derives against its units' own FormIDs.
    std::shared_ptr<CB::core::codec::HkyArchive> baseArc;
    {
        const fs::path baseHky = dataDir / "community_behaviors" / "plugins" / "Skyrim.hky";
        std::string herr;
        baseArc = CB::core::codec::HkyArchive::LoadFromFile(baseHky.string(), herr);
        if (!baseArc) { r.error = "base bundle not readable at " + baseHky.string() + " (install Community Behaviors first): " + herr; return r; }
    }
    bconv::BaseUnitCompiler baseCompiler(baseArc);

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

    /// @brief Each bundle's master table `[self, skyrim, masters…]` by lower bundle name (filled before the bundle
    ///        loop) — a dependent merges each master's already-written patch unit through that master's table.
    std::unordered_map<std::string, std::vector<std::string>> masterTableOf;
    /// @brief Convert ONE graph's Nemesis patch dirs into a patch unit at `outUnit`, derived against the graph's base
    ///        unit with the bundle's masters' patch units merged in (load order).
    auto convertGraph = [&](const std::string& g, bool firstPerson, const std::string& bname,
                            const std::vector<std::string>& masters, const std::vector<std::string>& masterPatchDirs,
                            const std::vector<std::string>& patchDirs, const std::string& outUnit) -> bool {
        const std::string label = bname + "/" + (firstPerson ? "_1stperson/" : "") + g;
        const std::string& baseXml = firstPerson ? fpBaseXmlOf(g) : baseXmlOf(g);
        if (baseXml.empty()) { ++r.skipped; say("  " + label + ": no template — skipped"); return false; }
        const std::string prefix = ToLower(graphServePathOf(g, firstPerson));
        CB::core::decompile::ParsedHky base;
        std::string err;
        if (!baseCompiler.Parse(prefix, base, err)) { ++r.skipped; say("  " + label + ": base unit unreadable — " + err); return false; }
        const auto& bones = baseCompiler.BonesFor(prefix);
        const auto* bp = bones.empty() ? nullptr : &bones;
        for (const auto& m : masters) {
            const fs::path mu = plugins / (m + ".hky") / fs::path(prefix);
            std::error_code me;
            if (!fs::is_directory(mu, me)) continue;
            if (!CB::core::decompile::MergeHkyPatch(base, CB::core::common::DiskUnitSource(mu.string()), schemaReg,
                                                    { "skyrim" }, masterTableOf[ToLower(m)], err, bp)) {
                ++r.skipped; say("  " + label + ": master '" + m + "' patch doesn't merge — " + err); return false;
            }
        }
        const auto md = CB::core::decompile::ConvertModDelta(baseXml, masterPatchDirs, patchDirs, base, { "skyrim" },
                                                             masterTableOf[ToLower(bname)], schemaReg, outUnit, bp);
        if (!md.ok) { ++r.skipped; say("  " + label + ": patch FAILED — " + md.error); return false; }
        if (md.patched == 0 && md.added == 0) return false;
        ++r.deltas;
        return true;
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

            std::vector<std::uint8_t> bytes;
            std::string rerr;
            if (!CB::core::codec::ReadHavokFile(winner.string(), bytes, &rerr)) {
                ++r.skipped; say("  " + rel + ": loose-file READ FAILED — " + rerr); continue;
            }
            std::vector<std::string> modded;
            if (!WithFirstOfClass(bytes, schemaReg, "hkbCharacterStringData",
                                  [&](CB::core::codec::io::SchemaObject& so) { modded = so.FieldRef("animationNames").strs; })) {
                ++r.skipped; say("  " + rel + ": loose-file is not a readable character"); continue;
            }
            std::unordered_set<std::string> have;
            if (const auto src = baseArc->source(ToLower("meshes/" + fs::path(rel).generic_string())))
                if (const auto cs = CB::core::decompile::ReadCharacterStrings(*src))
                    for (const auto& a : cs->animationNames) have.insert(ToLower(a));
            std::vector<std::string> adds;
            for (const auto& a : modded) if (have.insert(ToLower(a)).second) adds.push_back(a);
            if (adds.empty()) continue;   // replaced file, but same roster — nothing to carry

            // Roster patch at the character unit's real serve path (rel mirrors the meshes tree,
            // e.g. "actors/horse/characters/horse.hkx"), unioned onto the base character at runtime.
            const std::string charServe = "meshes/" + fs::path(rel).generic_string();
            const int rAdded = AddRosterToCharacterUnit(rosters, cfBundle, charServe, adds);
            if (rAdded <= 0) continue;
            ++r.charDeltas; charFilesAny = true;
            say("  CharacterFiles: " + rel + " — " + std::to_string(rAdded) +
                " roster addition(s) (loose replacement).");
        }
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
    {
            for (const auto& u : baseArc->units()) {
                if (cancel) { r.error = "cancelled"; return r; }
                if (u.kind != CB::core::codec::HkyArchive::UnitKind::Behavior) continue;
                if (u.prefix.rfind("meshes/actors/character/", 0) == 0) continue;   // Nemesis-owned
                std::error_code we;
                const fs::path winner = dataDir / fs::path(u.prefix);               // VFS-winning file
                if (!fs::is_regular_file(winner, we)) continue;                      // graph not present

                // Owning mod for this precompiled prefix (top-priority shipper), else anonymous.
                const auto pm = prefixToMod.find(ToLower(fs::path(u.prefix).generic_string()));
                const bool        attributed = pm != prefixToMod.end();
                const std::string bname      = opt.singleBundle ? kPandora : (attributed ? pm->second : std::string("BehaviorFiles"));
                const fs::path    outBundle  = plugins / (bname + ".hky");

                /// @note Derived against the base UNIT (its own FormIDs), so matched overrides land on the served
                ///       base's nodes by construction — no vanilla binary or numbering oracle involved.
                const fs::path unit = outBundle / fs::path(u.prefix);
                std::vector<std::uint8_t> mb;
                std::string derr;
                CB::core::decompile::ParsedHky base;
                CB::core::decompile::HkyPatchStats st;
                const auto& bones = baseCompiler.BonesFor(u.prefix);
                if (!CB::core::codec::ReadHavokFile(winner.string(), mb, &derr) || !baseCompiler.Parse(u.prefix, base, derr) ||
                    !CB::core::decompile::DeriveHkyPatch(base, { "skyrim" }, { ToLower(bname), "skyrim" }, mb, schemaReg,
                                                         unit.string(), derr, bones.empty() ? nullptr : &bones, &st)) {
                    ++r.skipped; say("  " + bname + ": " + u.prefix + " — DERIVE FAILED: " + derr); continue;
                }
                if (st.patched == 0 && st.added == 0) { fs::remove_all(unit, we); continue; }  // vanilla — nothing to carry
                ++r.deltas;
                if (attributed) precompiledBundles.insert(bname); else behFilesAny = true;
                say("  " + bname + ": " + u.prefix + " — " + std::to_string(st.patched) + " changed + " +
                    std::to_string(st.added) + " new node(s).");

                // ROSTER (Phase 1): a precompiled loose graph (the horse's real ship path) carries its
                // new clips ONLY in the binary — the derive above emits the node delta but no roster, so
                // without this the new animations never enter the character's animationNames and bind to
                // nothing. Read the loose graph's clip animationNames and union them into the graph's own
                // character unit(s) (actors/horse -> characters/horse.hkx) as a roster patch.
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
                            for (const auto& s : serves) rAdded += AddRosterToCharacterUnit(rosters, outBundle, s, refs.animationNames);
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
    // code's patch for a graph is the SAME unit (its root, vocabulary, base-node overrides), so
    // converting them one-at-a-time last-writer-clobbers all but the last — dropping e.g. tdmlen's
    // `TDM_Pitch` variable declaration, which 0_master then references undeclared → universal
    // A-pose. Merging all a bundle's codes' patch dirs in ONE convert per graph unions their vocab. Excluded engine (Pandora/Nemesis) codes are dropped.
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

    // FormId minting: which Nemesis codes does a set of patch dirs REFERENCE (#<code>$<N> tokens)? Scan
    // the raw patch text — a code appearing here that isn't the bundle's own is a FOREIGN master. Recurse
    // each code dir (all graphs) so a master's index is stable across every graph of the bundle.
    auto scanReferencedCodes = [&](const std::vector<std::string>& codes) {
        std::set<std::string> refs;
        for (const auto& c : codes) {
            std::error_code se;
            for (fs::recursive_directory_iterator it(codeDirOf(c), se), end; !se && it != end; it.increment(se)) {
                if (!it->is_regular_file()) continue;
                const std::string fn = it->path().filename().string();
                if (fn.empty() || fn[0] != '#' || it->path().extension() != ".txt") continue;
                std::ifstream in(it->path(), std::ios::binary);
                const std::string t((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                for (std::size_t p = t.find('#'); p != std::string::npos; p = t.find('#', p + 1)) {
                    std::size_t i = p + 1, dollar = std::string::npos;
                    for (; i < t.size(); ++i) {
                        const char ch = t[i];
                        if (ch == '$') { dollar = i; }
                        else if (!(std::isalnum((unsigned char)ch) || ch == '_')) break;
                    }
                    if (dollar != std::string::npos && dollar > p + 1 && i > dollar + 1)   // "#<code>$<digits>"
                        refs.insert(ToLower(t.substr(p + 1, dollar - (p + 1))));
                }
            }
        }
        return refs;
    };
    /// @note Every bundle's masters (the bundles owning the foreign codes its patches reference, by load priority),
    ///       then the bundles ordered masters-first: a dependent derives against its masters' written patch units.
    std::unordered_map<std::string, std::vector<std::string>> bundleMasters;  // bundle -> ordered master names
    for (const auto& bname : bundleOrder) {
        const std::vector<std::string>& codeList = bundleCodes[bname];
        std::set<std::string> ownLower;
        for (const auto& c : codeList) ownLower.insert(ToLower(c));
        std::set<std::string> foreignBundlesSet;
        for (const auto& rc : scanReferencedCodes(codeList))
            if (!ownLower.count(rc))
                if (const auto it = codeToMod.find(rc); it != codeToMod.end() && it->second != bname) foreignBundlesSet.insert(it->second);
        std::vector<std::string> mastersOrdered(foreignBundlesSet.begin(), foreignBundlesSet.end());
        std::stable_sort(mastersOrdered.begin(), mastersOrdered.end(), [&](const std::string& a, const std::string& b) {
            const auto pa = bundlePriority.find(a), pb = bundlePriority.find(b);
            const int va = pa != bundlePriority.end() ? pa->second : 0;
            const int vb = pb != bundlePriority.end() ? pb->second : 0;
            return va != vb ? va < vb : a < b;
        });
        if (!mastersOrdered.empty()) bundleMasters[bname] = mastersOrdered;
        auto& table = masterTableOf[ToLower(bname)];
        table = { ToLower(bname), "skyrim" };
        for (const auto& m : mastersOrdered) table.push_back(ToLower(m));
    }
    {
        std::vector<std::string>        ordered;
        std::unordered_set<std::string> done, active;
        std::function<void(const std::string&)> visit = [&](const std::string& b) {
            if (done.count(b)) return;
            if (!active.insert(b).second) { say("  WARN: bundle '" + b + "' is in a master cycle — converted without the cycle's order."); return; }
            if (const auto it = bundleMasters.find(b); it != bundleMasters.end())
                for (const auto& m : it->second) if (bundleCodes.count(m)) visit(m);
            active.erase(b);
            if (done.insert(b).second) ordered.push_back(b);
        };
        for (const auto& b : bundleOrder) visit(b);
        bundleOrder = std::move(ordered);
    }
    /// @note Masters are TRANSITIVE: a master's patch names its own masters' nodes, so merging it (and tagfile-merging
    ///       its Nemesis patches) needs them first. Closure in conversion order, which is masters-first.
    {
        std::unordered_map<std::string, std::size_t> pos;
        for (std::size_t i = 0; i < bundleOrder.size(); ++i) pos[bundleOrder[i]] = i;
        for (const auto& b : bundleOrder) {
            const auto it = bundleMasters.find(b);
            if (it == bundleMasters.end()) continue;
            std::set<std::string> all(it->second.begin(), it->second.end());
            for (const auto& m : it->second)
                if (const auto mt = bundleMasters.find(m); mt != bundleMasters.end()) all.insert(mt->second.begin(), mt->second.end());
            all.erase(b);
            std::vector<std::string> closure(all.begin(), all.end());
            std::stable_sort(closure.begin(), closure.end(), [&](const std::string& x, const std::string& y) {
                const auto px = pos.find(x), py = pos.find(y);
                return (px != pos.end() ? px->second : 0) < (py != pos.end() ? py->second : 0);
            });
            it->second = closure;
            auto& table = masterTableOf[ToLower(b)];
            table = { ToLower(b), "skyrim" };
            for (const auto& m : closure) table.push_back(ToLower(m));
        }
    }

    for (const auto& bname : bundleOrder) {
        if (cancel) { r.error = "cancelled"; return r; }
        const std::vector<std::string>& codeList = bundleCodes[bname];
        const fs::path                  bundle   = plugins / (bname + ".hky");
        bool any = false;
        std::string codeLabel;   // "tdmh+tdmlen+tdmv" for logs
        for (const auto& c : codeList) { if (!codeLabel.empty()) codeLabel += "+"; codeLabel += c; }
        static const std::vector<std::string> kNoMasters;
        const auto mit = bundleMasters.find(bname);
        const std::vector<std::string>& masters = mit != bundleMasters.end() ? mit->second : kNoMasters;
        /// @brief The masters' own patch dirs for graph `sub` ("<g>" or "_1stperson/<g>"), in master order.
        auto masterDirsOf = [&](const fs::path& sub) {
            std::vector<std::string> dirs;
            for (const auto& m : masters)
                if (const auto bc = bundleCodes.find(m); bc != bundleCodes.end())
                    for (const auto& c : bc->second)
                        if (fs::is_directory(codeDirOf(c) / sub, ec)) dirs.push_back((codeDirOf(c) / sub).string());
            return dirs;
        };

        // 2a) behavior graph deltas — MERGE every owning code's patch dir for a graph in ONE convert.
        int n = 0;
        for (const auto& g : graphs) {
            std::vector<std::string> patchDirs;
            for (const auto& c : codeList) {
                const fs::path patch = codeDirOf(c) / g;
                if (fs::is_directory(patch, ec)) patchDirs.push_back(patch.string());
            }
            if (!patchDirs.empty() && convertGraph(g, false, bname, masters, masterDirsOf(g), patchDirs, unitOut(bname, g))) ++n;
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
            if (!patchDirs.empty() &&
                convertGraph(g, true, bname, masters, masterDirsOf(fs::path("_1stperson") / g), patchDirs, fpUnitOut(bname, g))) ++n;
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
            // a horse graph's clips land in the horse character's roster, a first-person
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
                    if (!fs::exists(unitDir / "graph.yaml", ue)) {
                        std::vector<std::uint8_t> ub;
                        std::string               uerr;
                        const std::string unitKey = ToLower(hkx.lexically_normal().lexically_relative(dataDir.lexically_normal()).generic_string());
                        const auto& ubones = baseCompiler.BonesFor(unitKey);
                        if (CB::core::codec::ReadHavokFile(hkx.string(), ub, &uerr) &&
                            CB::core::decompile::DecompileBehaviorReflective(ub, schemaReg, unitDir.string(), uerr,
                                                                             ubones.empty() ? nullptr : &ubones))
                            ++ownedUnits;
                        else
                            fs::remove_all(unitDir, ue);   // not a readable behavior — leave no junk
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
                for (const auto& e : fs::recursive_directory_iterator(refsDir, de)) {
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
                    for (const auto& e : fs::recursive_directory_iterator(clipsDir, ce)) {
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
                // Each character's roster additions — a collected name that IS vanilla drops out when the
                // patch is derived against the base character (FlushRosters).
                int         totalAdded = 0;
                std::size_t totalNames = 0;
                for (const auto& [serve, names] : byServe) {
                    totalNames += names.size();
                    totalAdded += AddRosterToCharacterUnit(rosters, bundle, serve, names);
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

        /// @note 2c') anim-data ships nothing: the runtime derives each clip from the behavior graph and its root
        ///       motion from the packaged animation's `motion:` (a mod's shipped adsf disagrees with AMR motion).

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

    // 3b) animationnames/ -> character roster patches — the set-data leg (2b) still emits bundle-root
    //     animationnames/<stem>.txt roster drops. Re-file every one into its character's roster, keyed by
    //     file stem -> base character serve path, then remove the shim folder so no bundle ships it. Roster
    //     lines are UnescapeXml'd to literal (the roster holds literal paths; the shim drops may be
    //     XML-escaped). A stem with no base character is left in place + warned.
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
                const int added = AddRosterToCharacterUnit(rosters, bi->path(), sit->second, names);
                if (added > 0) { ++movedFiles; movedNames += added; }
            }
            if (!anyUnroutable) fs::remove_all(anDir, pe);   // retire the shim folder from the bundle
        }
        if (movedFiles > 0)
            say("  roster: re-filed " + std::to_string(movedFiles) + " animationnames drop(s) into "
                "character units (" + std::to_string(movedNames) + " roster line(s)).");
        if (const int n = FlushRosters(rosters, baseCompiler, *baseArc, schemaReg, log))
            say("  roster: wrote " + std::to_string(n) + " character patch unit(s).");
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

        // Authoritative pre-conversion record: the resolved order + each mod's master table (self=0,
        // Skyrim=1, cross-bundle 2+). Reproducible companion to the runtime loadorder.txt above.
        if (plan.ok) {
            std::ofstream planOut(loPath.parent_path() / "loadorder.plan.txt", std::ios::binary);
            planOut << EmitLoadOrderText(plan);
            say("  loadorder.plan.txt: wrote resolved plan (" + std::to_string(plan.mods.size()) + " mod(s)).");
        }
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
        // Masters = Skyrim (index 1) + any FOREIGN bundles this bundle's patches referenced (discovered
        // during the graph converts, ordered by load priority; indices 2+). BR's BuildMasterTable rebuilds
        // [self(0), skyrim(1), these…(2+)] to resolve the FormId indices this bundle emitted. Skyrim first.
        std::vector<std::string> masters{ "Skyrim" };
        if (const auto it = bundleMasters.find(code); it != bundleMasters.end())
            for (const auto& m : it->second) masters.push_back(m);
        WriteManifest(plugins / (code + ".hky"),
                      ni.name.empty() ? code : ni.name,
                      ni.version, ni.author, masters);
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
    r.ok = true;
    return r;
}

/// @brief The Havok behavior-system kinds the converter ingests, classified BY CONTENT (the packfile's
///   __classnames__ section at the file head), never by path — path-matching silently dropped files at
///   unexpected paths (e.g. the first-person skeleton characterassets/skeletonfirst.hkx).
enum class HkxKind { Other, Skeleton, Behavior, Character, Project, Animation };
/// @brief Classify a .hkx from its leading bytes.
/// @note Bytes-based so archive (BSA) entries — which never exist as files — classify identically to loose ones.
/// @warning The animation check MUST precede the skeleton check: some exporters embed hkaSkeleton in an animation
///   file (census 2026-09-27: 14 mod animations, e.g. OAR sets, + vanilla CC fishing cast/ready), while no real
///   skeleton file carries spline/interleaved animation data (all 330 vanilla skeletons). Skeleton-first silently
///   dropped those animations from the bundles.
HkxKind PeekHkxKindHead(std::string_view head) {
    if (head.find("hkbProjectData")   != std::string::npos) return HkxKind::Project;
    if (head.find("hkbCharacterData") != std::string::npos) return HkxKind::Character;
    if (head.find("hkbBehaviorGraph") != std::string::npos) return HkxKind::Behavior;
    if (head.find("hkaSplineCompressedAnimation")      != std::string::npos ||
        head.find("hkaInterleavedUncompressedAnimation") != std::string::npos) return HkxKind::Animation;
    if (head.find("hkaSkeleton")      != std::string::npos) return HkxKind::Skeleton;
    return HkxKind::Other;
}
HkxKind PeekHkxKind(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return HkxKind::Other;
    std::string head(1u << 16, '\0');
    f.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(f.gcount()));
    return PeekHkxKindHead(head);
}
HkxKind PeekHkxKind(const std::vector<std::uint8_t>& bytes) {
    return PeekHkxKindHead(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                            std::min<std::size_t>(bytes.size(), 1u << 16)));
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
/// @param poseGate true = keep the unit only if it recompiles pose-faithfully (<= kBakeMaxRotDeg): the master's
///   mode, whose poses may be compiled + served. false = keep EVERY animation that decompiles: the per-mod mode.
/// @note Per-mod bundles package every decompilable animation, not only faithful or root-motion ones: the unit is
///   what the runtime roster scan, the adsf root-motion read and the future OAR replacement all consume, so a
///   skipped unit silently vanishes from all three. Pose fidelity only matters to compile/serve, which the
///   bundle's manifest (compile_animations) gates separately.
static AnimBakeOutcome BakeAnimationUnit(const std::vector<std::uint8_t>& abytes,
                                         const std::filesystem::path&     unitPath,
                                         const std::filesystem::path&     stageDir,
                                         std::string&                     reason,
                                         bool                             poseGate = true) {
    namespace fs = std::filesystem;
    std::error_code ec;
    constexpr double kBakeMaxRotDeg = 0.5;   // character tree maxes 0.13deg after the codec fixes
    const fs::path bakeTmp = stageDir / "anim_bake";
    fs::remove_all(bakeTmp, ec); fs::create_directories(bakeTmp, ec);
    const auto dc = CB::core::anim::DecompileAnimation(abytes, bakeTmp);
    if (!dc.ok) { reason = dc.error; return AnimBakeOutcome::Skip; }   // incl. "not spline-compressed"

    if (!poseGate) {
        fs::create_directories(unitPath.parent_path(), ec);
        std::ifstream src(bakeTmp / "animation.yaml", std::ios::binary);
        std::ofstream dst(unitPath, std::ios::binary | std::ios::trunc);
        dst << src.rdbuf();
        return AnimBakeOutcome::Ok;
    }
    try {
        const auto ref = CB::core::anim::AnimationYamlLoader::Load(bakeTmp / "animation.yaml");
        const auto rc  = CB::core::anim::CompileAnimation(ref, 30);   // fps ignored: ref carries numFrames
        if (!rc.ok) { reason = "recompile: " + rc.error; return AnimBakeOutcome::Skip; }

        // pose gate: re-decompile the recompiled bytes and compare rotations to `ref`.
        const fs::path animRt = stageDir / "anim_rt"; fs::remove_all(animRt, ec); fs::create_directories(animRt, ec);
        if (!CB::core::anim::DecompileAnimation(rc.bytes, animRt).ok) { reason = "re-decompile"; return AnimBakeOutcome::Skip; }
        const auto rt = CB::core::anim::AnimationYamlLoader::Load(animRt / "animation.yaml");
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

    // The shared schema registry (CB::core::schema::SharedRegistry) must already be armed by the caller
    // (main.cpp points it at <exe>/Havok before --build-base / --regen-master) so the schema-native
    // animation round-trip pass can assemble + decompile. If it is not, the anim pass reports the
    // registry error per file and counts them as failures — it does not abort the corpus walk.

    /// @note Stage in a PRIVATE temp root (per process) so the deep unit tree stays under MAX_PATH and nothing else
    ///       can write into it mid-build; only after packing is the unpacked <Name>.hky/ tree copied to
    ///       keepStagingDir (when set). Staging straight into the shared SCT_BASE_MASTER_DIR let another project's
    ///       regen (Skyrim Content Tools, same env var) wipe and rewrite units under a running CB pack — 34 units of
    ///       a deployed Skyrim.hky came out in the retired format. The bundle's name IS the output's filename.
    const std::string bundle    = fs::path(outHky).filename().string();
    const bool        keepStage = !keepStagingDir.empty();
    const fs::path    stage     = fs::temp_directory_path(ec) /
                                  ("cb_base_build_" + fs::path(bundle).stem().string() + "_" + std::to_string(_getpid()));
    const fs::path    stageHky  = stage / bundle;
    // Baked native ANIMATIONS ride in the bundle itself, reunited with their behaviors/characters/projects,
    // so their ROOT MOTION can feed the adsf; whether they COMPILE/serve is gated per bundle by the manifest
    // compile flag. The kept root is SHARED by every regenerated bundle (and cb_deploy's diff unpack), so
    // only this bundle's own subtree is cleared.
    fs::remove_all(stage, ec);
    fs::create_directories(stageHky / "meshes", ec);

    // NOTE: the singlefile decomposes are now FLAT per-project (keyed by the cache project-name stem),
    // landing in the two "<name>.txt/" folders — so the old project/character -> actor-root maps that
    // placed motion/movesets under actors/<actor>/ are gone (the folders don't need them).

    auto* sreg = CB::core::schema::SharedRegistry();
    if (!sreg) { r.error = "schema registry not armed"; return r; }

    /// @note RIG BINDING before any emit: a graph's bone-named fields render through the rig its characters bind
    ///       (project characterFilenames -> character rigName / behaviorFilename), read from the source binaries —
    ///       the same binding the runtime rebuilds from the emitted units (Resolver / BaseUnitCompiler).
    CB::core::skeleton::RigIndex                    rigIndex;
    std::map<std::string, std::vector<std::string>> rigBones;   // skeleton serve key -> bone names (original case)
    {
        std::map<std::string, fs::path> projects, characters;   // serve key -> source file
        for (fs::recursive_directory_iterator it(meshes, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe) || fe) continue;
            std::string key = "meshes/" + fs::relative(it->path(), meshes, fe).generic_string();
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (key.size() < 4 || key.compare(key.size() - 4, 4, ".hkx") != 0) continue;
            switch (PeekHkxKind(it->path())) {
                case HkxKind::Project:   projects[key] = it->path(); break;
                case HkxKind::Character: characters[key] = it->path(); break;
                case HkxKind::Skeleton: {
                    std::vector<std::uint8_t> b; std::vector<CB::core::skeleton::SkeletonData> sk;
                    if (CB::core::codec::ReadHavokFile(it->path().string(), b, nullptr) &&
                        CB::core::skeleton::LoadSkeletonsFromHkx(b.data(), b.size(), sk, nullptr) && !sk.empty()) {
                        auto& names = rigBones[key];
                        for (const auto& bone : sk[0].bones) names.push_back(bone.name);
                    }
                    break;
                }
                default: break;
            }
        }
        for (const auto& [projKey, file] : projects) {
            std::vector<std::uint8_t> pb;
            std::vector<std::string>  chars;
            if (CB::core::codec::ReadHavokFile(file.string(), pb, nullptr))
                WithFirstOfClass(pb, *sreg, "hkbProjectStringData",
                                 [&](CB::core::codec::io::SchemaObject& so) { chars = so.FieldRef("characterFilenames").strs; });
            rigIndex.AddProject(projKey, chars,
                [&](const std::string& charUnit) -> std::optional<CB::core::skeleton::CharacterRig> {
                    const auto c = characters.find(charUnit);
                    std::vector<std::uint8_t> cb;
                    if (c == characters.end() || !CB::core::codec::ReadHavokFile(c->second.string(), cb, nullptr)) return std::nullopt;
                    std::optional<CB::core::skeleton::CharacterRig> rig;
                    WithFirstOfClass(cb, *sreg, "hkbCharacterStringData", [&](CB::core::codec::io::SchemaObject& so) {
                        rig = CB::core::skeleton::CharacterRig{ so.FieldRef("rigName").str, so.FieldRef("behaviorFilename").str };
                    });
                    return rig;
                },
                [&](const std::string& rig) { return rigBones.count(rig) != 0; });
        }
        rigIndex.Finalize();
    }
    const auto bonesFor = [&](const std::string& serveKey) -> const std::vector<std::string>* {
        const auto* rigs = rigIndex.RigsFor(serveKey);
        if (!rigs || rigs->empty()) return nullptr;
        const auto b = rigBones.find(rigs->front());
        return b == rigBones.end() || b->second.empty() ? nullptr : &b->second;
    };

    say("== Building " + bundle + " (every unit under " + meshes.string() + ") ==");
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

        // Everything — behaviors, characters, projects, skeletons AND animations — bakes into the one bundle.
        const fs::path unit = stageHky / "meshes" / fs::path(rel);

        // SKELETON: hkaSkeleton -> bonelist.yaml + bones/ unit (SkeletonImport -> EmitSkeletonYamlTree).
        // The runtime builds each rig's bone table from these (Resolver m_skeletons); without one, every
        // bone-named field in that actor's graphs fails to parse.
        if (kind == HkxKind::Skeleton) {
            std::vector<std::uint8_t> sbytes; std::string srerr;
            std::vector<CB::core::skeleton::SkeletonData> sk; std::string skerr;
            if (CB::core::codec::ReadHavokFile(it->path().string(), sbytes, &srerr) &&
                CB::core::skeleton::LoadSkeletonsFromHkx(sbytes.data(), sbytes.size(), sk, &skerr) && !sk.empty()) {
                // Attach per-bone ragdoll physics (mass/radius/capsule/joint) BEFORE emit. Without this the
                // YAML tree is anim-only, CompileSkeletonFull derives 0 ragdoll bones, and BR's SERVED
                // skeleton has no ragdoll skeleton (skels[1]) → hkbRagdollDriver can't bind → every humanoid
                // bind-poses instead of ragdolling on death. Mirrors doSkeletonDecompileTree (the CLI verb);
                // this master-regen path is a SECOND skeleton HKX->YAML emitter and must round-trip the same.
                std::string perr;
                if (!CB::core::skeleton::ReadSkeletonPhysics(sbytes.data(), sbytes.size(), sk[0], &perr))
                    say("  note: skeleton physics read failed for " + rel + " (" + perr + ") — anim-only (no ragdoll derive).");
                std::string eerr;
                if (CB::core::skeleton::EmitSkeletonYamlTree(sk[0], unit, &eerr)) ++r.skeletons;
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
            if (!CB::core::codec::ReadHavokFile(it->path().string(), abytes, &arerr)) {
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

        /// @note BEHAVIOR / CHARACTER / PROJECT — a complete reflective unit (self-owned FormIDs, encounter order), its
        ///       bone-named fields rendered through the rig its characters bind. A mod's patch is derived against
        ///       THIS unit's identity, so no template numbering is involved.
        std::vector<std::uint8_t> bytes;
        std::string rerr;
        if (!CB::core::codec::ReadHavokFile(it->path().string(), bytes, &rerr)) { ++r.failed; continue; }
        std::string derr;
        if (!CB::core::decompile::DecompileBehaviorReflective(bytes, *sreg, unit.string(), derr, bonesFor("meshes/" + lower))) {
            ++r.failed;
            say("  decompile FAILED: " + rel + " — " + derr);
            continue;
        }
        if      (kind == HkxKind::Behavior)  ++r.behaviors;
        else if (kind == HkxKind::Character) ++r.characters;
        else if (kind == HkxKind::Project)   ++r.projects;
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
    say("== editable caches (per-project YAML) ==");
    {
        std::ifstream sf(meshes / "animationsetdatasinglefile.txt", std::ios::binary);
        std::string   setText((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
        if (!setText.empty()) {
            try {
                auto           parsed  = CB::core::animsetdata::ParseSingleFile(setText);
                const fs::path setRoot  = stageHky / "meshes" / "animationsetdatasinglefile.txt";
                fs::create_directories(setRoot, ec);   // index.yaml (+ setdata/ fallback); sets live in char units

                // EXTRACT the per-condition animation MEMBERSHIP to authorable paths: build a
                // (folder,file)CRC -> path index from the datasource meshes, then reverse each set's
                // baked CRCs to real animation paths (residue tokens for the ~0.1% not in the source).
                // Replaces the old "derive the whole roster per set" runtime bloat that corrupted saves.
                std::vector<std::string> cands;
                { std::error_code wec;
                  for (fs::recursive_directory_iterator it(meshes, wec), end; !wec && it != end; it.increment(wec)) {
                      if (!it->is_regular_file(wec)) continue;
                      std::string extn = it->path().extension().string();
                      for (char& c : extn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                      if (extn != ".hkx") continue;
                      std::string rel = fs::relative(it->path(), meshes, wec).string();
                      for (char& c : rel) if (c == '/') c = '\\';
                      cands.push_back("meshes\\" + rel);
                  } }
                const auto crcIndex = CB::core::animsetdata::BuildCrcIndex(cands);
                std::size_t nResolved = 0, nResidue = 0;
                for (auto& proj : parsed.projects) {
                    CB::core::animsetdata::ResolveSetdataPaths(proj, crcIndex);
                    for (const auto& s : proj.sets)
                        for (const auto& a : s.animations) (a.compare(0, 4, "@crc") == 0 ? nResidue : nResolved)++;
                }

                // index.yaml — the project ORDER + exact header strings (load-bearing manifest).
                const std::string idx = CB::core::animsetdata::EmitSetdataIndexYaml(parsed);
                std::ofstream(setRoot / "index.yaml", std::ios::binary)
                    .write(idx.data(), static_cast<std::streamsize>(idx.size()));

                /// @note Each project's sets (gate/equip/attacks + the extracted `animations`) are one file,
                ///       setdata/<projectstem>.yaml, self-identifying (carries `project:`) — keyed by PROJECT (a
                ///       character can host several). A unit folder holds only its graph. The runtime re-hashes the
                ///       paths (no derive, no roster union).
                int units = 0;
                fs::create_directories(setRoot / "setdata", ec);
                for (const auto& proj : parsed.projects) {
                    if (proj.sets.empty()) continue;
                    const std::string stem = CB::core::animsetdata::StemForHeader(proj.header);
                    if (stem.empty()) continue;
                    const std::string body = CB::core::animsetdata::EmitSetdataUnitYaml(proj);
                    std::ofstream(setRoot / "setdata" / (stem + ".yaml"), std::ios::binary)
                        .write(body.data(), static_cast<std::streamsize>(body.size()));
                    ++units;
                }
                say("  decomposed animationsetdatasinglefile.txt/ -> index.yaml + " +
                    std::to_string(units) + " setdata/ (" + std::to_string(nResolved) + " anim paths, " +
                    std::to_string(nResidue) + " residue; " +
                    std::to_string(parsed.projects.size()) + " projects indexed)");
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
                const auto     parsed  = CB::core::animdata::ParseSingleFile(animText);
                const fs::path animRoot = stageHky / "meshes" / "animationdatasinglefile.txt";
                fs::create_directories(animRoot, ec);

                // Resolve each project's character (roster source) by actor-folder co-location over the
                // just-decomposed stageHky tree, so clips are keyed by animation name (index resolved
                // from the roster at compile) instead of a hardwired magic number. Still needed for the
                // index.yaml `character:` refs and the motion reunion below.
                const auto pcs = CB::core::animdata::LoadProjectCharacters((stageHky / "meshes").string(),
                    [](const fs::path& unit) {
                        const auto cs = CB::core::decompile::ReadCharacterStrings(CB::core::common::DiskUnitSource(unit));
                        return cs ? cs->animationNames : std::vector<std::string>{};
                    },
                    [](const fs::path& unit) {
                        std::ifstream g(unit / "graph.yaml", std::ios::binary);
                        const std::string t((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
                        return CB::core::codec::HkyArchive::KindOfGraphYaml(t) == CB::core::codec::HkyArchive::UnitKind::Project;
                    });
                /// @note A project's character is the one its OWN asset list names ("Characters Wolf\\Wolf.hkx") — the
                ///       co-location heuristic above falls back to the first character of a shared actor folder, which
                ///       bound WolfProject to the dog (and mapped the wolf's motion records through the dog's roster).
                std::map<std::string, CB::core::animdata::ProjectCharacter> projChar;
                for (const auto& proj : parsed.projects) {
                    const std::string stem = CB::core::animdata::StemForProjectName(proj.name);
                    const auto pit = pcs.find(stem);
                    if (pit == pcs.end()) continue;
                    CB::core::animdata::ProjectCharacter pc = pit->second;
                    const fs::path folder = fs::path(pc.ref).parent_path().parent_path();   // the project's folder
                    for (const auto& asset : proj.assetPaths) {
                        std::string a = asset;
                        std::replace(a.begin(), a.end(), '\\', '/');
                        if (ToLower(a).rfind("characters", 0) != 0) continue;
                        const fs::path unit = stageHky / "meshes" / folder / a;
                        const auto cs = CB::core::decompile::ReadCharacterStrings(CB::core::common::DiskUnitSource(unit));
                        if (cs && !cs->animationNames.empty()) pc = { (folder / a).generic_string(), cs->animationNames };
                        break;
                    }
                    projChar.emplace(stem, std::move(pc));
                }
                std::map<std::string, std::string> charRefByStem;
                for (const auto& [stem, pc] : pcs) charRefByStem.emplace(stem, pc.ref);
                for (const auto& [stem, pc] : projChar) charRefByStem[stem] = pc.ref;

                // index.yaml — project manifest incl. the per-project `character:` ref.
                const std::string idx = CB::core::animdata::EmitAnimdataIndexYaml(parsed, charRefByStem);
                std::ofstream(animRoot / "index.yaml", std::ios::binary)
                    .write(idx.data(), static_cast<std::streamsize>(idx.size()));

                // FULL DERIVE (release item 4): the clips/ + motion/ folders are NO LONGER SHIPPED.
                // The runtime rebuilds the entire base cache from the graph + the CB-native animation
                // units (DeriveBaseAnimData): clip generators from the base behaviours, annotation
                // triggers + duration + root motion from each animation.hkx (AnimationDef). Only
                // index.yaml (the project manifest + roster refs) rides in the hky; the adsf CACHE is
                // pure derived output. Motion is carried onto the animations by the reunion below.
                say("  decomposed animationdatasinglefile.txt/ -> index.yaml ONLY (clips+motion DERIVED at runtime, " +
                    std::to_string(parsed.projects.size()) + " projects)");

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
                    std::size_t reunited = 0, unbaked = 0, folded = 0, kept = 0;
                    std::set<std::string> handled;
                    for (const auto& proj : parsed.projects) {
                        if (!proj.hasAnimData || proj.motions.empty()) continue;
                        const std::string stem = CB::core::animdata::StemForProjectName(proj.name);
                        const auto pit = projChar.find(stem);
                        if (pit == projChar.end()) continue;                // unresolved -> clips kept raw index
                        const auto& roster = pit->second.roster;
                        /// @note The actor root is the character ref minus its last two segments: the characters folder
                        ///       may be suffixed ("characters female/", "characters dog/") — a "/characters/" search
                        ///       skipped those projects, so their motion was never reunited.
                        const std::string actorRoot = fs::path(lc(pit->second.ref)).parent_path().parent_path().generic_string();
                        /// @note Each motion record goes to the animation its CLIP plays: vanilla's clip (by name) owns record
                        ///       k; the vanilla graph's clip generator of that name names the animation; the roster slot with
                        ///       that FILENAME (the engine's LoadClips key — the gendered file for a shared graph) is the
                        ///       file. Vanilla's animIndex is NOT always today's roster position: the werewolf's adsf predates
                        ///       5 roster removals, so an index map put TurnRight60's motion on WW_WalkForwardLeft.
                        ///       Records no clip reaches fall back to the index.
                        const auto fileKey = [&](const std::string& p) {
                            const auto s = p.find_last_of("/\\");
                            return lc(s == std::string::npos ? p : p.substr(s + 1));
                        };
                        std::unordered_map<std::string, std::size_t> slotOf;
                        for (std::size_t i = 0; i < roster.size(); ++i) slotOf.emplace(fileKey(roster[i]), i);
                        std::unordered_map<std::string, std::string> clipAnim;   // clip-generator name -> animationName
                        {
                            std::set<fs::path> dirs;
                            for (const auto& asset : proj.assetPaths) {
                                std::string a = asset;
                                std::replace(a.begin(), a.end(), '\\', '/');
                                if (lc(a).rfind("behaviors", 0) == 0) dirs.insert((meshes / actorRoot / a).parent_path());
                            }
                            for (const auto& d : dirs)
                                for (fs::directory_iterator di(d, ec), dend; !ec && di != dend; di.increment(ec)) {
                                    if (lc(di->path().extension().string()) != ".hkx") continue;
                                    std::vector<std::uint8_t> bytes;
                                    if (!CB::core::codec::ReadHavokFile(di->path().string(), bytes, nullptr)) continue;
                                    try {
                                        CB::core::codec::PackFileDeserializer des;
                                        des.ObjectFactory = CB::core::codec::io::MakeSchemaFactory(*sreg);
                                        CB::core::codec::BinaryReaderEx br(false, true, bytes);
                                        des.DeserializePartially(br);
                                        CB::core::codec::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
                                        for (const auto& o : des.ConstructAllOfClass(dr, "hkbClipGenerator"))
                                            if (const auto* c = dynamic_cast<const CB::core::codec::io::SchemaObject*>(o.get()))
                                                clipAnim.emplace(SoStr(*c, "name"), SoStr(*c, "animationName"));
                                    } catch (...) {}
                                }
                            ec.clear();
                        }
                        std::unordered_map<std::string, const CB::core::animdata::MotionRecord*> motionByIndex;
                        for (const auto& m : proj.motions) motionByIndex.emplace(m.animIndex, &m);
                        std::vector<std::pair<std::size_t, const CB::core::animdata::MotionRecord*>> assign;   // roster slot -> record
                        std::set<std::string> viaClip;
                        for (const auto& c : proj.clips) {
                            const auto mi = motionByIndex.find(c.animIndex);
                            const auto ca = clipAnim.find(c.name);
                            if (mi == motionByIndex.end() || ca == clipAnim.end()) continue;
                            const auto s = slotOf.find(fileKey(ca->second));
                            if (s == slotOf.end()) continue;
                            assign.emplace_back(s->second, mi->second);
                            viaClip.insert(c.animIndex);
                        }
                        for (const auto& m : proj.motions) {
                            if (viaClip.count(m.animIndex)) continue;
                            char* end = nullptr; const long i = std::strtol(m.animIndex.c_str(), &end, 10);
                            if (end && *end == '\0' && i >= 0 && (std::size_t)i < roster.size()) assign.emplace_back((std::size_t)i, &m);
                        }
                        for (const auto& [slot, mp] : assign) {
                            const auto& m = *mp;
                            if (roster[slot].empty()) continue;
                            const std::string canon = CB::core::animdata::CanonicalAnimPath(actorRoot, roster[slot]);
                            const fs::path animYaml = stageHky / "meshes" / canon;   // baked anims live in the bundle
                            std::error_code fe;
                            if (!fs::exists(animYaml, fe)) { ++unbaked; continue; }   // not baked -> vanilla loose keeps its adsf motion
                            if (!handled.insert(canon).second) continue;   // the male/female duplicate: identical record
                            std::ifstream in(animYaml, std::ios::binary);
                            std::string cur((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                            in.close();
                            std::string ferr;
                            switch (CB::core::anim::FinalizeUnitMotion(cur, m, &ferr)) {
                                case CB::core::anim::UnitMotionForm::Folded:   ++folded; break;
                                case CB::core::anim::UnitMotionForm::Explicit: ++kept; break;
                                default: say("  motion reunion: " + canon + " unreadable — " + ferr); continue;
                            }
                            std::ofstream(animYaml, std::ios::binary | std::ios::trunc) << cur;
                            ++reunited;
                        }
                    }
                    say("  motion reunion: " + std::to_string(reunited) + " animation(s) reunited with their motion record — " +
                        std::to_string(folded) + " folded onto [Root] (fold gate passed), " + std::to_string(kept) +
                        " kept explicit (still, or the root keeps visual travel); " + std::to_string(unbaked) +
                        " referenced-but-unbaked, left to vanilla loose.");
                }
            } catch (const std::exception& e) {
                say(std::string("  WARN: animationdata decompose failed: ") + e.what());
            }
        }
    }

    say("  packing -> " + outHky + " ...");
    std::string zerr;
    if (!sct::util::ZipDir(stageHky.string(), outHky, zerr)) { r.error = "pack failed: " + zerr; return r; }

    if (keepStage) {
        const fs::path kept = fs::path(keepStagingDir) / bundle;
        std::error_code ke;
        fs::remove_all(kept, ke);
        fs::create_directories(kept.parent_path(), ke);
        fs::rename(stageHky, kept, ke);   // same volume: instant
        if (ke) {                         // across volumes (temp on C:, the kept root on D:): copy
            ke.clear();
            fs::copy(stageHky, kept, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ke);
        }
        if (ke) say("  WARN: could not keep the unpacked tree at " + kept.string() + " (" + ke.message() + ")");
        else    say("  kept unpacked tree: " + kept.string());
    }
    fs::remove_all(stage, ec);   // the private staging root never outlives the build
    say("  built: " + outHky);
    r.ok = true;
    return r;
}

/// @brief Package EVERY animation each enabled mod contributes into its <modName>.hky (or the one shared
///   `targetBundle` in Pandora mode) as attributed single-file units — the per-mod sibling of BuildBaseBundle's
///   animation leg.
/// @note Sources = the mod's LOOSE meshes\ files + the archives the ENGINE loads for it via the plugin rule
///   (ResolveModArchivesFor). Loose beats any archive and a later-loaded archive beats an earlier one — the
///   engine's precedence inside one mod. INI-listed/built-in archives are base-game content: they belong to the
///   master, never a mod bundle (hence also the reserved bundle name "Skyrim").
/// @note Shared-bundle (Pandora) mode walks mods highest-priority first, so the FIRST mod to write a path owns it;
///   a lower-priority mod never overwrites it (it did before: last-writer-wins inverted MO2 priority).
/// @note FAIL-SAFE: a skip/fail writes no unit and the engine keeps the original file, so this can never break the
///   behavior/skeleton bundles built alongside.
/// @warning Archive-sourced units carry the archive's LOWERCASED path (BSA case is unrecoverable); loose-sourced
///   units keep their on-disk case.
static void PackageModAnimations(const Mo2Layout& mo2, const fs::path& plugins,
                                 const fs::path& stageRoot, const LogFn& log,
                                 const std::string& targetBundle) {
    namespace bsa = CB::core::codec::bsa;
    const auto say = [&](const std::string& s) { if (log) log(s); };
    std::error_code ec;
    const fs::path stage = stageRoot / "modanim_stage";
    fs::create_directories(stage, ec);

    std::unordered_map<std::string, std::vector<ModArchiveInfo>> pluginArchivesByMod;   ///< ascending load index
    for (auto& a : ResolveModArchivesFor(mo2))
        if (a.counted && a.loadIndex >= 0) pluginArchivesByMod[a.modName].push_back(std::move(a));

    struct Source {
        fs::path          loose;             ///< set for a loose file
        std::size_t       archive = 0;       ///< index into `archives` for an archive entry
        const bsa::Entry* entry = nullptr;
        std::string       rel;               ///< "meshes/actors/.../x.hkx" — the unit path inside the bundle
    };
    std::unordered_set<std::string> sharedClaimed;   ///< lower(rel) already written into the shared bundle
    std::map<std::string, int>      skipReasons;
    std::vector<std::string>        unrecognized;    ///< .hkx under an animations/ folder that did NOT classify as an animation
    int totalOk = 0, totalSkip = 0, totalFail = 0, totalFromArchive = 0, modsWithAnims = 0;
    std::vector<std::uint8_t> abytes;

    for (const std::string& modName : mo2.enabledTopFirst) {
        const fs::path    modRoot    = mo2.modsDir / modName;
        const std::string bundleName = targetBundle.empty() ? modName : targetBundle;
        const auto        archIt     = pluginArchivesByMod.find(modName);
        const bool        hasLoose   = fs::is_directory(modRoot / "meshes", ec);
        if (!hasLoose && archIt == pluginArchivesByMod.end()) continue;
        if (ToLower(bundleName) == "skyrim") {
            say("  skipped mod '" + modName + "': bundle name 'Skyrim' is reserved for the master "
                "(its base-game content is imported by the master regen).");
            continue;
        }
        const fs::path bundle = plugins / (bundleName + ".hky");

        std::map<std::string, Source> sources;   ///< lower(rel) -> the winning source for that path
        if (hasLoose)
            for (fs::recursive_directory_iterator it(modRoot / "meshes", ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const fs::path& p = it->path();
                if (ToLower(p.extension().string()) != ".hkx") continue;
                std::error_code rec;
                const std::string rel = fs::relative(p, modRoot, rec).generic_string();
                if (rec || rel.empty()) continue;
                sources.emplace(ToLower(rel), Source{ p, 0, nullptr, rel });
            }

        std::vector<bsa::Archive> archives;   ///< must outlive `sources`: Source::entry points into these
        if (archIt != pluginArchivesByMod.end()) {
            const auto& list = archIt->second;
            archives.reserve(list.size());
            for (auto a = list.rbegin(); a != list.rend(); ++a) {
                std::string err;
                if (auto arc = bsa::Archive::Open(a->archive, &err)) archives.push_back(std::move(*arc));
                else { say("  archive unreadable: " + a->archive + " — " + err); ++totalFail; }
            }
            for (std::size_t ai = 0; ai < archives.size(); ++ai)
                for (const auto& e : archives[ai].Entries()) {
                    if (e.path.rfind("meshes\\", 0) != 0 || e.path.size() < 4 ||
                        e.path.compare(e.path.size() - 4, 4, ".hkx") != 0) continue;
                    std::string rel = e.path;
                    std::replace(rel.begin(), rel.end(), '\\', '/');
                    sources.emplace(rel, Source{ {}, ai, &e, rel });
                }
        }

        int modOk = 0, modSkip = 0, modFail = 0, modFromArchive = 0;
        for (const auto& [key, src] : sources) {
            if (!targetBundle.empty() && sharedClaimed.count(key)) continue;
            std::string err;
            if (src.entry) {
                if (!archives[src.archive].Extract(*src.entry, abytes, &err)) {
                    say("  extract failed: " + src.rel + " — " + err);
                    ++modFail;
                    continue;
                }
                if (PeekHkxKind(abytes) != HkxKind::Animation) {
                    if (key.find("/animations/") != std::string::npos) unrecognized.push_back(modName + ": " + src.rel);
                    continue;
                }
            } else {
                if (PeekHkxKind(src.loose) != HkxKind::Animation) {
                    if (key.find("/animations/") != std::string::npos) unrecognized.push_back(modName + ": " + src.rel);
                    continue;
                }
                if (!CB::core::codec::ReadHavokFile(src.loose, abytes, &err)) { ++modFail; continue; }
            }
            std::string reason;
            switch (BakeAnimationUnit(abytes, bundle / fs::path(src.rel), stage, reason, /*poseGate=*/false)) {
                case AnimBakeOutcome::Ok:
                    ++modOk;
                    if (src.entry) ++modFromArchive;
                    if (!targetBundle.empty()) sharedClaimed.insert(key);
                    break;
                case AnimBakeOutcome::Skip: ++modSkip; ++skipReasons[reason.substr(0, std::min<std::size_t>(reason.find(':'), 60))]; break;
                case AnimBakeOutcome::Fail: ++modFail; break;
            }
        }
        if (modOk || modSkip || modFail) {
            modsWithAnims += modOk ? 1 : 0;
            say("  mod animations: " + modName + " -> " + std::to_string(modOk) + " unit(s) (" +
                std::to_string(modFromArchive) + " from archives), " + std::to_string(modSkip) + " skipped, " +
                std::to_string(modFail) + " failed.");
        }
        totalOk += modOk; totalSkip += modSkip; totalFail += modFail; totalFromArchive += modFromArchive;
    }
    fs::remove_all(stage, ec);
    say("Mod animations: " + std::to_string(totalOk) + " unit(s) across " + std::to_string(modsWithAnims) + " mod(s) (" +
        std::to_string(totalFromArchive) + " from archives), " + std::to_string(totalSkip) + " skipped, " +
        std::to_string(totalFail) + " failed.");
    for (const auto& [why, n] : skipReasons) say("    skip reason: " + std::to_string(n) + " x " + why);
    if (!unrecognized.empty()) {
        say("  WARNING: " + std::to_string(unrecognized.size()) + " .hkx under an animations/ folder did not classify as an "
            "animation (not packaged — invisible to the roster scan):");
        for (std::size_t i = 0; i < unrecognized.size() && i < 20; ++i) say("    " + unrecognized[i]);
    }
}

bool PackageAllModAnimations(const std::string& mo2Instance, const std::string& pluginsDir, const LogFn& log) {
    const Mo2Layout mo2 = ResolveMo2Layout(mo2Instance);
    if (!mo2.ok) {
        if (log) log("not an MO2 instance (no profiles/mods, or an empty modlist): " + mo2Instance);
        return false;
    }
    std::error_code ec;
    fs::create_directories(pluginsDir, ec);
    PackageModAnimations(mo2, pluginsDir, pluginsDir, log, std::string());
    return true;
}

bool SerializerRoundTrip(const std::string& meshesDir, const std::string& outRoot, const LogFn& log) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    auto* reg = CB::core::schema::SharedRegistry();
    if (!reg) { say("serializer-roundtrip: schema registry not armed"); return false; }
    std::error_code ec;
    int n = 0, same = 0, fails = 0;
    for (fs::recursive_directory_iterator it(meshesDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".hkx" || PeekHkxKind(it->path()) != HkxKind::Behavior) continue;
        std::vector<std::uint8_t> src, canon; std::string err;
        if (!CB::core::codec::ReadHavokFile(it->path().string(), src, &err) ||
            !CB::core::codec::io::RoundtripHkx(src, *reg, canon, err)) { ++fails; say("  FAIL " + it->path().string() + " — " + err); ++n; continue; }
        const std::string rel = fs::relative(it->path(), meshesDir, fe).generic_string();
        if (canon == src) { ++same; ++n; continue; }
        const fs::path d = fs::path(outRoot) / std::to_string(n);
        fs::create_directories(d, ec);
        std::ofstream(d / "src.bin", std::ios::binary).write(reinterpret_cast<const char*>(src.data()), static_cast<std::streamsize>(src.size()));
        std::ofstream(d / "canon.bin", std::ios::binary).write(reinterpret_cast<const char*>(canon.data()), static_cast<std::streamsize>(canon.size()));
        std::size_t i = 0;
        while (i < src.size() && i < canon.size() && src[i] == canon[i]) ++i;
        // Same GRAPH? Reflective emit of both (ids follow graph traversal, not file layout) must match file-for-file.
        std::string e1, e2;
        const bool okA = CB::core::decompile::DecompileBehaviorReflective(src,   *reg, (d / "src").string(),   e1);
        const bool okB = CB::core::decompile::DecompileBehaviorReflective(canon, *reg, (d / "canon").string(), e2);
        bool sameGraph = okA && okB;
        if (sameGraph) {
            std::set<std::string> rels;
            for (const char* side : { "src", "canon" })
                for (fs::recursive_directory_iterator jt(d / side, fe), jend; !fe && jt != jend; jt.increment(fe))
                    if (jt->is_regular_file()) rels.insert(fs::relative(jt->path(), d / side).generic_string());
            auto slurp = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); };
            for (const auto& r : rels) if (slurp(d / "src" / r) != slurp(d / "canon" / r)) { sameGraph = false; break; }
        }
        say("  DIFF " + std::to_string(n) + "\t" + rel + "\tfirst " + std::to_string(i) + "\tsize " +
            std::to_string(src.size()) + " -> " + std::to_string(canon.size()) + (sameGraph ? "\tSAME-GRAPH" : "\tGRAPH-DIFFERS"));
        ++n;
    }
    say("serializer-roundtrip: " + std::to_string(same) + "/" + std::to_string(n) + " identical to the source (" +
        std::to_string(fails) + " failed); differing pairs under " + outRoot);
    return fails == 0;
}

bool MultiLayerRoundTripAll(const std::string& modsDir, const std::string& templatesDir, const std::string& workRoot,
                            const LogFn& log, const std::string& baseHky) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    auto* reg = CB::core::schema::SharedRegistry();
    if (!reg) { say("multilayer-roundtrip: schema registry not armed"); return false; }
    std::optional<bconv::BaseUnitCompiler> rigs;
    if (!baseHky.empty()) {
        std::string herr;
        if (auto arc = CB::core::codec::HkyArchive::LoadFromFile(baseHky, herr)) rigs.emplace(arc);
    }
    // code -> Nemesis mod dir, across every MO2 mod (<mod>/Nemesis_Engine/mod/<code>/)
    std::map<std::string, fs::path> codes;
    std::error_code ec;
    for (fs::directory_iterator it(modsDir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path nm = it->path() / "Nemesis_Engine" / "mod";
        std::error_code e2;
        if (!fs::is_directory(nm, e2)) continue;
        for (fs::directory_iterator jt(nm, e2), jend; !e2 && jt != jend; jt.increment(e2))
            if (jt->is_directory()) codes[jt->path().filename().string()] = jt->path();
    }
    int graphs = 0, same = 0, fails = 0;
    for (const std::string& g : ScanGraphs(templatesDir)) {
        std::vector<CB::core::decompile::MultiLayerMod> mods;
        for (const auto& [code, dir] : codes) {
            const fs::path gd = dir / g;
            std::error_code e3;
            if (!fs::is_directory(gd, e3)) continue;
            CB::core::decompile::MultiLayerMod m{ code, {} };
            std::vector<fs::path> files;
            for (fs::directory_iterator kt(gd, e3), kend; !e3 && kt != kend; kt.increment(e3)) {
                const std::string fn = kt->path().filename().string();
                if (kt->is_regular_file() && !fn.empty() && fn[0] == '#' && kt->path().extension() == ".txt") files.push_back(kt->path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& f : files) {
                std::ifstream in(f, std::ios::binary);
                std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (!s.empty()) m.patches.push_back(std::move(s));
            }
            if (const char* only = std::getenv("CB_MULTILAYER_ONLY"))   // debug: restrict to a comma list of codes
                if (("," + std::string(only) + ",").find("," + code + ",") == std::string::npos) continue;
            if (!m.patches.empty()) mods.push_back(std::move(m));
        }
        if (mods.size() < (std::getenv("CB_MULTILAYER_ONLY") ? 1u : 2u)) continue;   // a single layer is the patch-roundtrip gate's job
        std::ifstream xf(fs::path(templatesDir) / (g + ".xml"), std::ios::binary);
        const std::string xml((std::istreambuf_iterator<char>(xf)), std::istreambuf_iterator<char>());
        const std::vector<std::string>* bones = nullptr;
        if (rigs) if (const auto& b = rigs->BonesFor("meshes/actors/character/behaviors/" + g + ".hkx"); !b.empty()) bones = &b;
        const auto r = CB::core::decompile::MultiLayerRoundTrip(xml, mods, *reg, (fs::path(workRoot) / g).string(), bones);
        ++graphs;
        std::string who;
        for (const auto& [name, st] : r.perMod) who += (who.empty() ? "" : " ") + name + "(" + std::to_string(st.patched) + "+" + std::to_string(st.added) + ")";
        if (!r.ok) { ++fails; say("  FAIL " + g + " [" + std::to_string(mods.size()) + " mods] — " + r.error); continue; }
        say(std::string(r.identical ? "  SAME " : "  DIFF ") + g + " [" + std::to_string(mods.size()) + " mods] " + who +
            (r.detail.empty() ? "" : " | " + r.detail));
        same += r.identical;
    }
    say("multilayer-roundtrip: " + std::to_string(same) + "/" + std::to_string(graphs) +
        " graphs: every mod's derived patch, merged in order, == today's merge of all (" + std::to_string(fails) + " failed)");
    return fails == 0;
}

bool HkyMerge(const std::string& baseHky, const std::string& meshesDir, const std::string& rel, const std::string& outDir,
              const std::vector<std::string>& patchDirs, const LogFn& log) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    auto* reg = CB::core::schema::SharedRegistry();
    if (!reg) { say("hky-merge: schema registry not armed"); return false; }
    std::optional<bconv::BaseUnitCompiler> rigs;
    std::string err;
    if (auto arc = CB::core::codec::HkyArchive::LoadFromFile(baseHky, err)) rigs.emplace(arc);
    std::vector<std::uint8_t> bytes;
    if (!CB::core::codec::ReadHavokFile((fs::path(meshesDir) / rel).string(), bytes, &err)) { say("hky-merge: " + err); return false; }
    std::string serveKey = "meshes/" + fs::path(rel).generic_string();
    for (char& ch : serveKey) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));   // internal RigIndex key
    const std::vector<std::string>* bones = nullptr;
    if (rigs) if (const auto& b = rigs->BonesFor(serveKey); !b.empty()) bones = &b;
    if (!CB::core::decompile::MergeHkyUnits(bytes, patchDirs, *reg, outDir, err, bones)) { say("hky-merge: " + err); return false; }
    say("hky-merge: " + std::to_string(patchDirs.size()) + " unit(s) merged onto " + rel + (bones ? "" : " (no rig)") + " -> " + outDir);
    return true;
}

bool PatchRoundTripAll(const std::string& vanillaMeshes, const std::string& moddedMeshes, const std::string& workRoot,
                       const LogFn& log, const std::string& baseHky) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    auto* reg = CB::core::schema::SharedRegistry();
    if (!reg) { say("patch-roundtrip: schema registry not armed"); return false; }
    std::optional<bconv::BaseUnitCompiler> rigs;
    if (!baseHky.empty()) {
        std::string herr;
        if (auto arc = CB::core::codec::HkyArchive::LoadFromFile(baseHky, herr)) rigs.emplace(arc);
    }
    std::error_code ec;
    std::map<std::string, fs::path> vanilla;   // lowercase rel -> path (internal match key; the files keep their case)
    for (fs::recursive_directory_iterator it(vanillaMeshes, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        std::string rel = fs::relative(it->path(), vanillaMeshes, fe).generic_string();
        for (char& ch : rel) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        vanilla[rel] = it->path();
    }
    int n = 0, same = 0, fails = 0;
    for (fs::recursive_directory_iterator it(moddedMeshes, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        std::string rel = fs::relative(it->path(), moddedMeshes, fe).generic_string();
        std::string key = rel;
        for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (key.size() < 4 || key.compare(key.size() - 4, 4, ".hkx") != 0) continue;
        const HkxKind kind = PeekHkxKind(it->path());
        if (kind != HkxKind::Behavior && kind != HkxKind::Character) continue;
        auto v = vanilla.find(key);
        if (v == vanilla.end()) { say("  SKIP " + rel + " (no vanilla counterpart)"); continue; }
        std::vector<std::uint8_t> vb, mb; std::string err;
        if (!CB::core::codec::ReadHavokFile(v->second.string(), vb, &err) || !CB::core::codec::ReadHavokFile(it->path().string(), mb, &err)) { ++fails; continue; }
        const std::vector<std::string>* bones = nullptr;
        if (rigs) if (const auto& b = rigs->BonesFor("meshes/" + key); !b.empty()) bones = &b;
        const auto r = CB::core::decompile::PatchRoundTrip(vb, mb, *reg, (fs::path(workRoot) / std::to_string(n)).string(), bones);
        ++n;
        if (!r.ok) { ++fails; say("  FAIL " + std::to_string(n - 1) + " " + rel + " — " + r.error); continue; }
        say(std::string(r.identical ? "  SAME " : "  DIFF ") + std::to_string(n - 1) + " " + rel + " — patch: " +
            std::to_string(r.derived.patched) + " patched + " + std::to_string(r.derived.added) + " new node(s)" +
            (r.detail.empty() ? "" : " | " + r.detail));
        same += r.identical;
    }
    say("patch-roundtrip: " + std::to_string(same) + "/" + std::to_string(n) + " merge(base, derive(base, mod)) == mod (" +
        std::to_string(fails) + " failed). Work trees under " + workRoot);
    return fails == 0;
}

bool MotionFoldGate(const std::string& treeDir, const std::string& outCsv, const LogFn& log) {
    auto say = [&](const std::string& m) { if (log) log(m); };
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(treeDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ToLower(it->path().extension().string()) != ".hkx") continue;
        std::string rel = ToLower(it->path().generic_string());
        if (rel.find("/animations/") != std::string::npos) files.push_back(it->path());
    }
    struct Row { std::string rel; CB::core::anim::FoldCheck c; std::string skeleton; };
    std::vector<std::optional<Row>> rows(files.size());
    std::atomic<std::size_t> next{ 0 }, failed{ 0 };
    auto work = [&] {
        for (std::size_t i; (i = next++) < files.size();) {
            std::ifstream in(files[i], std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (text.find("\n  motion:") == std::string::npos) continue;
            try {
                const auto def = CB::core::anim::AnimationYamlLoader::LoadFromString(text, files[i].string());
                if (const auto c = CB::core::anim::CheckFoldRoundTrip(def, 0))
                    rows[i] = Row{ fs::relative(files[i], treeDir, ec).generic_string(), *c, def.skeleton };
            } catch (...) { ++failed; }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) pool.emplace_back(work);
    for (auto& t : pool) t.join();

    /// @note "Clean" = Bethesda extracted all of the root's travel (the fold must round-trip); the rest keep visual root
    ///       motion that wholesale extraction turns into physical motion.
    constexpr double kCleanDrift = 0.01, kCleanYaw = 0.1;
    /// @note Tolerance = 0.01 + speed x one key-time step: the engine stores key times as IEEE halves (1/256 s at 4-8 s,
    ///       1/64 s past 16 s), so vanilla's keys and ours both sit on that grid — a difference below speed x step is the
    ///       engine's own representational limit, not a fold error.
    const auto within = [](double err, double rate, double step) { return err <= 0.01 + rate * std::max(step, 0.001); };
    std::size_t n = 0, clean = 0, rootNamed = 0, cleanPass = 0;
    double cleanErr = 0, cleanDeg = 0, visErr = 0, visDeg = 0;
    std::vector<const Row*> residual, cleanFails;
    std::size_t visFail = 0;
    std::ofstream csv(outCsv, std::ios::binary);
    csv << "unit,skeleton,extracted,motionErr,motionDeg,visualErr,visualDeg,rootDrift,rootYawDeg,worstTime\n";
    for (const auto& r : rows) {
        if (!r) continue;
        ++n;
        const auto& c = r->c;
        if (r->skeleton.find("[Root]") != std::string::npos) ++rootNamed;
        if (!within(c.visualErr, c.maxSpeed, c.keyTimeStep) || !within(c.visualDeg, c.maxTurn, c.keyTimeStep)) ++visFail;
        visErr = std::max(visErr, c.visualErr); visDeg = std::max(visDeg, c.visualDeg);
        if (c.rootDrift < kCleanDrift && c.rootYawDeg < kCleanYaw) {
            ++clean;
            cleanErr = std::max(cleanErr, c.motionErr); cleanDeg = std::max(cleanDeg, c.motionDeg);
            if (CB::core::anim::FoldRoundTrips(c)) ++cleanPass;
            else cleanFails.push_back(&*r);
        } else residual.push_back(&*r);
        csv << r->rel << "," << r->skeleton << "," << c.extracted << "," << c.motionErr << "," << c.motionDeg << ","
            << c.visualErr << "," << c.visualDeg << "," << c.rootDrift << "," << c.rootYawDeg << "," << c.worstTime << "\n";
    }
    say("motion-fold gate: " + std::to_string(n) + " unit(s) with motion (" + std::to_string(failed) + " unreadable); " +
        std::to_string(rootNamed) + " bound to a [Root] skeleton.");
    say("  on screen (all): " + std::to_string(n - visFail) + "/" + std::to_string(n) + " within tolerance (max " +
        std::to_string(visErr) + " units / " + std::to_string(visDeg) + " deg from vanilla).");
    say("  clean-root clips: " + std::to_string(cleanPass) + "/" + std::to_string(clean) + " round-trip (max " +
        std::to_string(cleanErr) + " units / " + std::to_string(cleanDeg) + " deg).");
    for (std::size_t i = 0; i < cleanFails.size() && i < 12; ++i)
        say("    FAIL " + cleanFails[i]->rel + "  " + std::to_string(cleanFails[i]->c.motionErr) + "u @" +
            std::to_string(cleanFails[i]->c.worstTime) + "s (speed " + std::to_string(cleanFails[i]->c.maxSpeed) + "u/s), " +
            std::to_string(cleanFails[i]->c.motionDeg) + "deg (turn " + std::to_string(cleanFails[i]->c.maxTurn) + "deg/s)");
    std::sort(residual.begin(), residual.end(), [](const Row* a, const Row* b) { return a->c.motionErr > b->c.motionErr; });
    std::size_t buckets[5] = {};
    for (const Row* r : residual) {
        const double e = r->c.motionErr;
        ++buckets[e < 0.1 ? 0 : e < 1 ? 1 : e < 10 ? 2 : e < 100 ? 3 : 4];
    }
    say("  root-residual clips: " + std::to_string(residual.size()) + " gain physical motion — <0.1u " + std::to_string(buckets[0]) +
        ", <1u " + std::to_string(buckets[1]) + ", <10u " + std::to_string(buckets[2]) + ", <100u " + std::to_string(buckets[3]) +
        ", >=100u " + std::to_string(buckets[4]) + ". Largest:");
    for (std::size_t i = 0; i < residual.size() && i < 12; ++i)
        say("    " + residual[i]->rel + "  +" + std::to_string(residual[i]->c.motionErr) + "u / " +
            std::to_string(residual[i]->c.motionDeg) + "deg");
    say("  per-unit CSV: " + outCsv);
    return failed == 0 && cleanPass == clean && visFail == 0;
}

bool AdsfProjectionCheck(const std::string& hkyPath, const std::string& vanillaTxt, const LogFn& log) {
    using CB::core::codec::io::SchemaObject;
    auto say = [&](const std::string& m) { if (log) log(m); };
    const auto* reg = CB::core::schema::SharedRegistry();
    std::string err;
    const auto arc = reg ? CB::core::codec::HkyArchive::LoadFromFile(hkyPath, err) : nullptr;
    if (!arc) { say("adsf-project: " + (reg ? err : std::string("schema registry not armed"))); return false; }
    const auto index = arc->file("meshes/animationdatasinglefile.txt/index.yaml");
    if (!index) { say("adsf-project: no animationdatasinglefile.txt/index.yaml in " + hkyPath); return false; }
    const auto headers = CB::core::animdata::ParseAnimdataIndexYaml(*index, err);

    bconv::BaseUnitCompiler units(arc);
    std::mutex mx;
    std::map<std::string, std::vector<CB::core::animdata::DeriveClipInput>> clipCache;
    std::unordered_map<std::string, std::optional<CB::core::anim::AnimationDef>> anims;
    CB::core::animdata::ProjectionSource src;
    src.roster = [&](const std::string& key) -> std::vector<std::string> {
        const auto s = arc->source(key);
        const auto cs = s ? CB::core::decompile::ReadCharacterStrings(*s) : std::nullopt;
        return cs ? cs->animationNames : std::vector<std::string>{};
    };
    src.clips = [&](const std::string& key) {
        if (const auto it = clipCache.find(key); it != clipCache.end()) return it->second;
        CB::core::decompile::ParsedHky g;
        std::string e;
        std::vector<CB::core::animdata::DeriveClipInput> c;
        if (units.Parse(key, g, e)) c = CB::core::compile::DeriveClipInputsFromGraph(g.root);
        return clipCache[key] = c;
    };
    src.behaviorUnits = [&] {
        std::vector<std::string> out;
        for (const auto& u : arc->units())
            if (u.kind == CB::core::codec::HkyArchive::UnitKind::Behavior) out.push_back(u.prefix);
        return out;
    };
    src.prefetch = [&](const std::vector<std::string>& paths) {
        std::vector<std::optional<CB::core::anim::AnimationDef>> parsed(paths.size());
        std::atomic<std::size_t> next{ 0 };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t)
            pool.emplace_back([&] {
                for (std::size_t i; (i = next++) < paths.size();)
                    if (const auto text = arc->file(paths[i])) {
                        try {
                            auto def = CB::core::anim::AnimationYamlLoader::LoadFromString(*text, paths[i]);
                            def.motion = CB::core::anim::ResolveRootMotion(def);
                            parsed[i] = std::move(def);
                        } catch (...) {}
                    }
            });
        for (auto& t : pool) t.join();
        for (std::size_t i = 0; i < paths.size(); ++i) anims[paths[i]] = std::move(parsed[i]);
    };
    src.animation = [&](const std::string& path) -> const CB::core::anim::AnimationDef* {
        auto it = anims.find(path);
        if (it == anims.end()) {
            src.prefetch({ path });
            it = anims.find(path);
        }
        return it != anims.end() && it->second ? &*it->second : nullptr;
    };

    CB::core::animdata::ProjectionStats st;
    const auto t0 = std::chrono::steady_clock::now();
    const auto ours = CB::core::animdata::ProjectAnimData(*reg, headers, src, &st, err);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (!ours) { say("adsf-project: projection failed — " + err); return false; }
    std::ifstream vin(vanillaTxt, std::ios::binary);
    const std::string vtext((std::istreambuf_iterator<char>(vin)), std::istreambuf_iterator<char>());
    const auto van = CB::core::codec::io::ReadMetaText(*reg, "AnimationDataSingleFile", vtext, err);
    if (!van) { say("adsf-project: vanilla unreadable — " + err); return false; }
    std::string outText;
    if (!CB::core::codec::io::WriteMetaText(*ours, outText, err)) { say("adsf-project: write refused — " + err); return false; }
    if (const char* dump = std::getenv("CB_ADSF_PROJECT_DUMP")) std::ofstream(dump, std::ios::binary) << outText;

    const auto obj = [](const std::shared_ptr<CB::core::codec::IHavokObject>& o) -> SchemaObject& { return static_cast<SchemaObject&>(*o); };
    std::map<std::string, SchemaObject*> vanProjects;
    for (const auto& p : van->FieldRef("projects").objs) vanProjects[obj(p).FieldRef("name").str] = &obj(p);
    std::size_t clipsBoth = 0, missing = 0, extra = 0, badIndex = 0, badSpeed = 0, badTriggers = 0, motionChecked = 0, badMotion = 0;
    std::vector<std::string> samples;
    const auto sample = [&](const std::string& s) { if (samples.size() < 16) samples.push_back(s); };
    for (const auto& po : ours->FieldRef("projects").objs) {
        SchemaObject& p = obj(po);
        const auto vp = vanProjects.find(p.FieldRef("name").str);
        if (vp == vanProjects.end() || p.FieldRef("hasAnimData").str != "1") continue;
        const std::string pname = p.FieldRef("name").str;
        const auto byName = [&](SchemaObject& proj) {
            std::map<std::string, SchemaObject*> m;
            for (const auto& c : proj.FieldRef("clips").objs) m.emplace(obj(c).FieldRef("name").str, &obj(c));
            return m;
        };
        const auto motionsOf = [&](SchemaObject& proj) {
            std::map<std::string, SchemaObject*> m;
            for (const auto& c : proj.FieldRef("motions").objs) m.emplace(obj(c).FieldRef("animIndex").str, &obj(c));
            return m;
        };
        auto oc = byName(p), vc = byName(*vp->second);
        auto om = motionsOf(p), vm = motionsOf(*vp->second);
        for (const auto& [name, v] : vc)
            if (!oc.count(name)) { ++missing; sample("missing " + pname + ": " + name); }
        for (const auto& [name, o] : oc) {
            const auto v = vc.find(name);
            if (v == vc.end()) { ++extra; continue; }
            ++clipsBoth;
            if (o->FieldRef("animIndex").str != v->second->FieldRef("animIndex").str) {
                ++badIndex; sample("animIndex " + pname + ": " + name + " " + o->FieldRef("animIndex").str + " vs " + v->second->FieldRef("animIndex").str);
            }
            if (o->FieldRef("playbackSpeed").str != v->second->FieldRef("playbackSpeed").str) ++badSpeed;
            if (o->FieldRef("triggers").strs != v->second->FieldRef("triggers").strs) ++badTriggers;
            /// @note Motion: the clip's own record on each side (by its animIndex), sampled by the engine evaluator.
            const auto mo = om.find(o->FieldRef("animIndex").str), mv = vm.find(v->second->FieldRef("animIndex").str);
            if (mv == vm.end()) continue;
            ++motionChecked;
            CB::core::animdata::MotionRecord a, b;
            a.translations = mv->second->FieldRef("translations").strs; a.rotations = mv->second->FieldRef("rotations").strs;
            if (mo != om.end()) { b.translations = mo->second->FieldRef("translations").strs; b.rotations = mo->second->FieldRef("rotations").strs; }
            const CB::core::anim::EngineMotionCurve ca(a), cb(b);
            const double dur = std::atof(mv->second->FieldRef("duration").str.c_str());
            double worst = 0;
            for (double t = 0; t <= dur + 1e-6; t += 1.0 / 60) {
                const auto x = ca.Sample(t), y = cb.Sample(t);
                worst = std::max(worst, std::sqrt((x.t[0]-y.t[0])*(x.t[0]-y.t[0]) + (x.t[1]-y.t[1])*(x.t[1]-y.t[1]) + (x.t[2]-y.t[2])*(x.t[2]-y.t[2])));
            }
            if (worst > 0.5) { ++badMotion; sample("motion " + pname + ": " + name + " off by " + std::to_string(worst) + "u"); }
        }
    }
    say("adsf-project: projected " + std::to_string(st.projects) + " anim project(s), " + std::to_string(st.clips) + " clip(s), " +
        std::to_string(st.motions) + " motion(s) in " + std::to_string(ms) + " ms; " + std::to_string(st.unresolved) +
        " clip(s) play an animation the roster lacks; written " + std::to_string(outText.size()) + " bytes (vanilla " +
        std::to_string(vtext.size()) + ").");
    for (const auto& u : st.unresolvedSample) say("  unresolved " + u);
    say("  vs vanilla: " + std::to_string(clipsBoth) + " clip(s) in both, " + std::to_string(missing) + " missing, " +
        std::to_string(extra) + " extra; animIndex differs " + std::to_string(badIndex) + ", speed " + std::to_string(badSpeed) +
        ", triggers " + std::to_string(badTriggers) + "; motion " + std::to_string(motionChecked - badMotion) + "/" +
        std::to_string(motionChecked) + " within 0.5u.");
    for (const auto& s : samples) say("    " + s);
    return missing == 0 && badIndex == 0 && badMotion == 0;
}

bool MetaTextRoundTrip(const std::vector<std::string>& files, const LogFn& log) {
    auto say = [&](const std::string& m) { if (log) log(m); };
    const auto* reg = CB::core::schema::SharedRegistry();
    if (!reg) { say("metatext-roundtrip: schema registry not armed"); return false; }
    bool all = true;
    for (const auto& path : files) {
        std::ifstream in(path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const bool setData = ToLower(path).find("animationset") != std::string::npos;
        const std::string cls = setData ? "AnimationSetDataSingleFile" : "AnimationDataSingleFile";
        std::string err, out;
        const auto root = CB::core::codec::io::ReadMetaText(*reg, cls, text, err);
        if (!root) { say("  FAIL read " + path + " — " + err); all = false; continue; }
        if (!CB::core::codec::io::WriteMetaText(*root, out, err)) { say("  FAIL write " + path + " — " + err); all = false; continue; }
        std::size_t diff = 0;
        while (diff < text.size() && diff < out.size() && text[diff] == out[diff]) ++diff;
        const bool same = out == text;
        /// @note Record counts vs the hand-written parsers: the schema reads the same structure, not just the same bytes.
        std::size_t projects = root->FieldRef("projects").objs.size(), records = 0, handRecords = 0, handProjects = 0;
        for (const auto& po : root->FieldRef("projects").objs) {
            auto& p = static_cast<CB::core::codec::io::SchemaObject&>(*po);
            if (setData) for (const auto& so : p.FieldRef("sets").objs) { (void)so; ++records; }
            else records += p.FieldRef("clips").objs.size() + p.FieldRef("motions").objs.size();
        }
        try {
            if (setData) {
                const auto hand = CB::core::animsetdata::ParseSingleFile(text);
                handProjects = hand.projects.size();
                for (const auto& p : hand.projects) handRecords += p.sets.size();
            } else {
                const auto hand = CB::core::animdata::ParseSingleFile(text);
                handProjects = hand.projects.size();
                for (const auto& p : hand.projects) handRecords += p.clips.size() + p.motions.size();
            }
        } catch (const std::exception& e) { say("  (hand-written parser failed: " + std::string(e.what()) + ")"); }
        const bool counts = projects == handProjects && records == handRecords;
        say(std::string(same && counts ? "  OK   " : "  FAIL ") + path + " — " + cls + ", " + std::to_string(projects) + " project(s), " +
            std::to_string(records) + " record(s)" + (counts ? "" : " (hand-written: " + std::to_string(handProjects) + " / " +
            std::to_string(handRecords) + ")") + (same ? ", byte-identical" : ", first difference at byte " + std::to_string(diff) +
            " (" + std::to_string(text.size()) + " -> " + std::to_string(out.size()) + " bytes)"));
        all = all && same && counts;
    }
    return all;
}

bool MotionStorageCheck(const std::string& newTree, const std::string& oldTree, const LogFn& log) {
    auto say = [&](const std::string& m) { if (log) log(m); };
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(oldTree, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && ToLower(it->path().extension().string()) == ".hkx" &&
            ToLower(it->path().generic_string()).find("/animations/") != std::string::npos)
            files.push_back(it->path());
    std::atomic<std::size_t> next{ 0 }, checked{ 0 }, folded{ 0 }, bad{ 0 }, missing{ 0 }, keysOld{ 0 }, keysNew{ 0 }, keysMax{ 0 };
    std::mutex mx;
    std::vector<std::string> worst;
    auto work = [&] {
        for (std::size_t i; (i = next++) < files.size();) {
            std::ifstream in(files[i], std::ios::binary);
            std::string oldText((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (oldText.find("\n  motion:\n") == std::string::npos) continue;
            const fs::path rel = fs::relative(files[i], oldTree, ec);
            std::ifstream in2(fs::path(newTree) / rel, std::ios::binary);
            if (!in2) { ++missing; continue; }
            std::string newText((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
            try {
                const auto oldDef = CB::core::anim::AnimationYamlLoader::LoadFromString(oldText, rel.string());
                auto newDef = CB::core::anim::AnimationYamlLoader::LoadFromString(newText, rel.string());
                if (!newDef.motion && !newDef.rootMotionVisual) ++folded;
                const auto got = CB::core::anim::ResolveRootMotion(newDef);
                const CB::core::anim::EngineMotionCurve want(*oldDef.motion), have(got ? *got : CB::core::animdata::MotionRecord{});
                keysOld += oldDef.motion->translations.size() + oldDef.motion->rotations.size();
                if (got) {
                    const std::size_t k = got->translations.size() + got->rotations.size();
                    keysNew += k;
                    for (std::size_t prevMax = keysMax; k > prevMax && !keysMax.compare_exchange_weak(prevMax, k);) {}
                }
                double err = 0, speed = 0;
                CB::core::anim::MotionPose prev = want.Sample(0);
                for (int f = 0; f < oldDef.numFrames; ++f) {
                    const double t = f * static_cast<double>(oldDef.frameDuration);
                    for (const double s : { t, t + 0.5 * oldDef.frameDuration }) {
                        if (s > oldDef.duration + 1e-6) continue;
                        const auto a = want.Sample(s), b = have.Sample(s);
                        err = std::max(err, std::sqrt((a.t[0]-b.t[0])*(a.t[0]-b.t[0]) + (a.t[1]-b.t[1])*(a.t[1]-b.t[1]) + (a.t[2]-b.t[2])*(a.t[2]-b.t[2])));
                    }
                    const auto p = want.Sample(t);
                    if (f) speed = std::max(speed, std::sqrt((p.t[0]-prev.t[0])*(p.t[0]-prev.t[0]) + (p.t[1]-prev.t[1])*(p.t[1]-prev.t[1]) + (p.t[2]-prev.t[2])*(p.t[2]-prev.t[2])) / oldDef.frameDuration);
                    prev = p;
                }
                ++checked;
                /// @note Same tolerance as the fold gate: 0.01 + speed x the engine's key-time step past 16 s (1/64 s).
                if (err > 0.01 + speed * (oldDef.duration >= 16 ? 1.0 / 64 : oldDef.duration >= 8 ? 1.0 / 128 : oldDef.duration >= 4 ? 1.0 / 256 : 1.0 / 512)) {
                    ++bad;
                    std::lock_guard<std::mutex> lk(mx);
                    if (worst.size() < 12) worst.push_back(rel.generic_string() + "  " + std::to_string(err) + "u (speed " + std::to_string(speed) + ")");
                }
            } catch (const std::exception& e) {
                ++bad;
                std::lock_guard<std::mutex> lk(mx);
                if (worst.size() < 12) worst.push_back(rel.generic_string() + "  unreadable: " + e.what());
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) pool.emplace_back(work);
    for (auto& t : pool) t.join();
    say("motion-storage check: " + std::to_string(checked - bad) + "/" + std::to_string(checked) + " unit(s) resolve to their vanilla motion (" +
        std::to_string(folded) + " from a folded root); " + std::to_string(missing) + " missing from the new tree; key lines " +
        std::to_string(keysNew) + " vs vanilla " + std::to_string(keysOld) + " (max " + std::to_string(keysMax) + " per record).");
    for (const auto& w : worst) say("  MISMATCH " + w);
    return bad == 0 && missing == 0;
}

bool HkyRoundTrip(const std::string& meshesDir, const std::string& workRoot, const LogFn& log, const std::string& baseHky,
                  bool allKinds) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    auto* reg = CB::core::schema::SharedRegistry();
    if (!reg) { say("hky-roundtrip: schema registry not armed"); return false; }
    std::optional<bconv::BaseUnitCompiler> rigs;
    if (!baseHky.empty()) {
        std::string herr;
        if (auto arc = CB::core::codec::HkyArchive::LoadFromFile(baseHky, herr)) rigs.emplace(arc);
        else say("hky-roundtrip: base bundle unreadable (" + herr + ") — bone fields stay numeric");
    }
    std::error_code ec;
    int n = 0, same = 0, canonOk = 0, fails = 0;
    std::map<std::string, std::pair<int, int>> perKind;   // kind -> (identical, total)
    for (fs::recursive_directory_iterator it(meshesDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".hkx") continue;
        const HkxKind kind = PeekHkxKind(it->path());
        const char* kindName = kind == HkxKind::Behavior ? "behavior" : kind == HkxKind::Character ? "character"
                             : kind == HkxKind::Project ? "project" : kind == HkxKind::Skeleton ? "skeleton" : nullptr;
        if (!kindName || (!allKinds && kind != HkxKind::Behavior)) continue;
        std::vector<std::uint8_t> bytes; std::string err;
        if (!CB::core::codec::ReadHavokFile(it->path().string(), bytes, &err)) { ++fails; continue; }
        const std::string rel = fs::relative(it->path(), meshesDir, fe).generic_string();
        std::string serveKey = "meshes/" + rel;
        for (char& ch : serveKey) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));   // internal RigIndex key
        const std::vector<std::string>* bones = nullptr;
        if (rigs) if (const auto& b = rigs->BonesFor(serveKey); !b.empty()) bones = &b;
        const auto r = CB::core::decompile::RoundTripReflective(bytes, *reg, (fs::path(workRoot) / std::to_string(n)).string(), bones);
        ++n;
        if (!r.ok) { ++fails; say("  FAIL " + rel + " — " + r.error); continue; }
        canonOk += r.canonIsSource;
        ++perKind[kindName].second;
        if (r.identical) { ++same; ++perKind[kindName].first; std::error_code re; fs::remove_all(fs::path(workRoot) / std::to_string(n - 1), re); continue; }
        say("  DIFF " + std::to_string(n - 1) + " " + rel + " — first byte " + std::to_string(r.firstDiff) +
            ", size delta " + std::to_string(r.sizeDelta) + (r.detail.empty() ? "" : " | " + r.detail));
    }
    for (const auto& [k, v] : perKind)
        say("  " + k + ": " + std::to_string(v.first) + "/" + std::to_string(v.second) + " byte-identical");
    say("hky-roundtrip: " + std::to_string(same) + "/" + std::to_string(n) + " byte-identical via the reflective .hky (" +
        std::to_string(canonOk) + "/" + std::to_string(n) + " serializer round-trips equal the source; " +
        std::to_string(fails) + " failed). Differing trees kept under " + workRoot);
    return fails == 0;
}

RegenResult RegenerateMaster(const std::string& vanillaMeshesDir, const std::string& templatesDir,
                             const std::string& outHky, bool strict,
                             const LogFn& log, const std::atomic<bool>& cancel,
                             const std::string& keepStagingDir) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    RegenResult r;
    std::error_code ec;

    const std::string bundle  = fs::path(outHky).filename().string();
    const fs::path    work    = fs::temp_directory_path(ec) / ("cb_regen_" + fs::path(bundle).stem().string() + "_" + std::to_string(_getpid()));
    const fs::path    tempHky = work / bundle;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    // 1) Build the whole bundle to a TEMP file (never touch outHky until it validates). keepStagingDir
    //    (optional) keeps the unpacked <Name>.hky/ tree there for observation — built regardless of the
    //    validation outcome below (you want to inspect it precisely when it DIDN'T validate).
    r.build = BuildBaseBundle(vanillaMeshesDir, tempHky.string(), log, cancel, templatesDir, keepStagingDir);
    if (!r.build.ok) { r.error = "build-base failed: " + r.build.error; fs::remove_all(work, ec); return r; }

    /// @note 2) SERVED-BYTES gate: every reflective unit of the fresh bundle (behavior / character / project) must parse
    ///       back through the runtime's own reader — bones bound through its rig, as the Resolver binds them — and
    ///       serialize byte-identical to its source binary's canonical re-serialization. That is exactly what the
    ///       runtime serves for a graph no mod touches. Drifting pairs are kept for inspection.
    say("");
    say("== Validating regenerated " + bundle + " (parse + serialize every unit vs its source) ==");
    std::string herr;
    auto arc = CB::core::codec::HkyArchive::LoadFromFile(tempHky.string(), herr);
    if (!arc) { r.error = "cannot reopen built bundle for validation: " + herr; fs::remove_all(work, ec); return r; }
    auto* sreg = CB::core::schema::SharedRegistry();
    if (!sreg) { r.error = "schema registry not armed"; fs::remove_all(work, ec); return r; }
    bconv::BaseUnitCompiler baseCompiler(arc);
    const fs::path driftKeep = fs::temp_directory_path(ec) / ("cb_regen_drift_" + fs::path(outHky).stem().string());
    fs::remove_all(driftKeep, ec);
    for (const auto& u : arc->units()) {
        if (cancel) { r.error = "cancelled"; fs::remove_all(work, ec); return r; }
        if (u.kind == CB::core::codec::HkyArchive::UnitKind::Skeleton || u.prefix.rfind("meshes/", 0) != 0) continue;
        const fs::path src = fs::path(vanillaMeshesDir) / u.prefix.substr(7);
        std::vector<std::uint8_t> srcBytes, canon, got;
        std::string e;
        if (!fs::is_regular_file(src, ec) || !CB::core::codec::ReadHavokFile(src.string(), srcBytes, &e)) continue;
        if (!CB::core::codec::io::RoundtripHkx(srcBytes, *sreg, canon, e)) { say("  " + u.prefix + ": source unreadable — " + e); continue; }
        ++r.checked;
        if (!baseCompiler.Compile(u.prefix, got, e)) {
            r.drift.push_back(u.prefix);
            say("  FAIL: " + u.prefix + " — " + e);
            continue;
        }
        if (got == canon) { ++r.faithful; continue; }
        r.drift.push_back(u.prefix);
        say("  DRIFT: " + u.prefix + " — served " + std::to_string(got.size()) + " bytes vs canonical " + std::to_string(canon.size()));
        const fs::path keep = driftKeep / fs::path(u.prefix).relative_path();
        fs::create_directories(keep, ec);
        std::ofstream(keep / "served.bin", std::ios::binary).write(reinterpret_cast<const char*>(got.data()), static_cast<std::streamsize>(got.size()));
        std::ofstream(keep / "canon.bin", std::ios::binary).write(reinterpret_cast<const char*>(canon.data()), static_cast<std::streamsize>(canon.size()));
    }
    if (!r.drift.empty()) say("  drift pairs kept under " + driftKeep.string());
    say("  served-bytes gate: " + std::to_string(r.faithful) + "/" + std::to_string(r.checked) +
        " unit(s) serve byte-identical to their source" + (r.drift.empty() ? "." : "; " + std::to_string(r.drift.size()) + " drift/fail."));

    // 3) Promote — or, in strict mode, refuse to write if anything drifted.
    if (!r.drift.empty() && strict) {
        r.error = "strict gate: " + std::to_string(r.drift.size()) + " unit(s) don't serve their source — bundle NOT written";
        say("  " + r.error);
        fs::remove_all(work, ec);
        return r;
    }
    fs::create_directories(fs::path(outHky).parent_path(), ec);
    fs::copy_file(tempHky, outHky, fs::copy_options::overwrite_existing, ec);
    if (ec) { r.error = "failed to write " + outHky + ": " + ec.message(); fs::remove_all(work, ec); return r; }
    fs::remove_all(work, ec);
    say("  " + bundle + " -> " + outHky + (r.drift.empty() ? "" : "  (written with " + std::to_string(r.drift.size()) +
        " drifting unit(s) — review the DRIFT / FAIL lines above)"));
    r.ok = true;
    return r;
}

}  // namespace bconv
