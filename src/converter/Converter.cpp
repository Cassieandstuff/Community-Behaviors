#include "Converter.h"

#include <codec/serialization/HavokFile.h>             // ReadHavokFile
#include <codec/formid/FormId.h>                        // CB::engine::codec::formid::SELF_INDEX (index convention)
#include <decompile/SkeletonImport.h>        // LoadSkeletonsFromHkx / ReadSkeletonPhysics (skeleton import)
#include <codec/format/SkeletonYaml.h>       // EmitSkeletonYamlTree — SkeletonData -> bonelist.yaml + bones/ unit
#include <codec/serialization/HkyArchive.h>     // Skyrim.hky base = the loose-derive vanilla source
#include <codec/format/AnimationSetData.h>     // parse vanilla animationsetdatasinglefile
#include <codec/format/AnimSetDataYaml.h>      // EmitMovesetsYaml (vanilla decompose)
#include <interface/AnimationData.h>        // parse vanilla animationdatasinglefile
#include <codec/format/AnimDataYaml.h>         // EmitMotionYaml (motion decompose)
#include <codec/serialization/packfile/PackFileDeserializer.h> // object-count gate for template matching + ConstructAllOfClass (pass 2d)
#include <codec/serialization/packfile/BinaryReaderEx.h>       // BinaryReaderEx — drives ConstructAllOfClass
#include <compile/AnimationCompiler.h>       // CB::engine::anim::CompileAnimation — recompile leg (schema-native)
#include <decompile/AnimationDecompiler.h>   // CB::engine::anim::DecompileAnimation — schema-native import leg
#include <codec/motion/RootMotion.h>
#include <codec/serialization/MetaText.h>
#include <compile/AnimDataProjection.h>
#include <compile/AnimDataFromBehavior.h>

#include <atomic>
#include <process.h>   // _getpid — a private per-process staging root
#include <mutex>
#include <thread>
#include <chrono>
#include <type_traits>
#include <codec/format/AnimationYamlLoader.h> // AnimationYamlLoader::Load — animation.yaml -> AnimationDef
#include <decompile/BehaviorDecompile.h>     // ConvertModDelta / DeriveHkyPatch / the reflective unit codec
#include <interface/reflection/HavokSchema.h>        // SchemaRegistry (Havok/ class descriptors)
#include <codec/serialization/HavokIo.h>     // io::MakeSchemaFactory / io::SchemaObject (schema decompile dispatch)
#include "BaseUnitCompiler.h"               // rig-bound base-unit compile (regen gates)
#include "NemesisSetDataConvert.h"   // CommunityBehaviors::asd::ConvertNemesisSetData
#include "FnisConverter.h"           // CommunityBehaviors::fnis::ConvertFnis
#include "BaseMaps.h"                // BuildBaseMaps — where each vanilla graph / character lives
#include "platform/Platform.h"       // ZipDir — pack the staged master into one Skyrim.hky
#include <codec/bsa/BsaArchive.h>
#include <codec/spline/SplineDecompressor.h>   // DecodeSpline — the CB-8 gate's float-section referee

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

namespace CB::converter {
namespace {

constexpr const char* kBehSub    = "meshes/actors/character/behaviors";
constexpr const char* kBehSub1st = "meshes/actors/character/_1stperson/behaviors";

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Read a string field off a generic SchemaObject by name (empty if absent / not a string field).
std::string SoStr(const CB::engine::codec::io::SchemaObject& so, const char* name) {
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
    if (!CB::engine::codec::ReadHavokFile(hkxPath, bytes, &err)) return false;
    // Schema-native: build the clip/RBG objects as generic SchemaObjects (schema factory) and read the
    // one field each off the field table by name — no typed hkbClipGenerator/hkbBehaviorReferenceGenerator.
    CB::engine::schema::SchemaRegistry* reg = CB::engine::schema::SharedRegistry();
    if (!reg) return false;
    try {
        CB::engine::codec::PackFileDeserializer des;
        des.ObjectFactory = CB::engine::codec::io::MakeSchemaFactory(*reg);
        CB::engine::codec::BinaryReaderEx       br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        des.DeserializePartially(br);
        const bool le = des._header.Endian == 0;
        const bool p8 = des._header.PointerSize == 8;
        {
            CB::engine::codec::BinaryReaderEx dr(le, p8, des.DataSectionBytes());
            for (const auto& o : des.ConstructAllOfClass(dr, "hkbClipGenerator"))
                if (const auto* c = dynamic_cast<const CB::engine::codec::io::SchemaObject*>(o.get())) {
                    const std::string an = SoStr(*c, "animationName");
                    if (!an.empty()) out.animationNames.push_back(an);
                }
        }
        {
            CB::engine::codec::BinaryReaderEx dr(le, p8, des.DataSectionBytes());
            for (const auto& o : des.ConstructAllOfClass(dr, "hkbBehaviorReferenceGenerator"))
                if (const auto* r = dynamic_cast<const CB::engine::codec::io::SchemaObject*>(o.get())) {
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

/// @brief The convertible graphs: one per tagfile template <g>.xml (the oracle's alignment source, always required).
/// @note The vanilla binary comes from the shipped Skyrim.hky base unit, never a template .hkx — the scan keys on .xml alone.
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
static bool WithFirstOfClass(const std::vector<std::uint8_t>& bytes, const CB::engine::schema::SchemaRegistry& reg,
                             std::string_view cls, const std::function<void(CB::engine::codec::io::SchemaObject&)>& fn) {
    try {
        CB::engine::codec::PackFileDeserializer des;
        des.ObjectFactory = CB::engine::codec::io::MakeSchemaFactory(reg);
        CB::engine::codec::BinaryReaderEx br(false, true, bytes);
        des.Deserialize(br);
        for (const auto& [off, o] : des.DeserializedObjects())
            if (auto* so = dynamic_cast<CB::engine::codec::io::SchemaObject*>(o.get()); so && cls == so->ClassName()) {
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
int FlushRosters(const RosterSink& sink, CB::converter::BaseUnitCompiler& base, const CB::engine::codec::HkyArchive& arc,
                 const CB::engine::schema::SchemaRegistry& reg, const LogFn& log) {
    const auto say = [&](const std::string& m) { if (log) log(m); };
    const auto fileKey = [](const std::string& n) {
        const auto p = n.find_last_of("\\/");
        return ToLower(p == std::string::npos ? n : n.substr(p + 1));
    };
    std::map<std::string, std::string>              groupOf;      // character serve key -> alignment group
    std::map<std::string, std::vector<std::string>> members;      // group -> character serve keys
    std::map<std::string, std::vector<std::string>> baseRoster;   // character serve key -> its base animationNames
    for (const auto& u : arc.units()) {
        if (u.kind != CB::engine::codec::HkyArchive::UnitKind::Character) continue;
        const auto src = arc.source(u.prefix);
        const auto cs  = src ? CB::engine::decompile::ReadCharacterStrings(*src) : std::nullopt;
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
            CB::engine::decompile::ParsedHky g, fresh;
            std::string err;
            std::vector<std::uint8_t> bytes;
            const auto& bones = base.BonesFor(serve);
            const fs::path unit = fs::path(bundleDir) / fs::path(serve);
            bool ok = base.Parse(serve, g, err);
            if (ok)
                CB::engine::decompile::GraphEdit(g, reg).ForEach("hkbCharacterStringData", [&](CB::engine::codec::io::SchemaObject& so) {
                    auto& roster = so.FieldRef("animationNames").strs;
                    roster.insert(roster.end(), add.begin(), add.end());
                });
            ok = ok && CB::engine::decompile::SerializeGraph(g, bytes, err) && base.Parse(serve, fresh, err) &&
                 CB::engine::decompile::DeriveHkyPatch(fresh, { "skyrim" }, { ToLower(fs::path(bundleDir).stem().string()), "skyrim" },
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
    const std::string docsUtf8 = CB::converter::platform::DocumentsFolder();
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

}  // namespace  (file-local helpers above; the plan API below is external in CB::converter)

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

/// @brief Run `f(i)` — or `f(i, worker)` — for every i in [0, n) across the hardware threads. `f` must touch only state of
///        its own index (or read-only shared state); results land in per-index slots so the caller can fold them in
///        order. `worker` (0..threads-1) names the calling thread, e.g. for a per-worker scratch dir.
template <class F>
static void ParallelFor(std::size_t n, F&& f) {
    std::atomic<std::size_t> next{ 0 };
    std::vector<std::thread> pool;
    const unsigned           nThreads = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < nThreads; ++t)
        pool.emplace_back([&, t] {
            for (std::size_t i; (i = next++) < n;) {
                if constexpr (std::is_invocable_v<F&, std::size_t, unsigned>) f(i, t);
                else f(i);
            }
        });
    for (auto& th : pool) th.join();
}

// Per-mod animation packaging (piece 1). Defined below BuildBaseBundle (needs PeekHkxKind/HkxKind/
// BakeAnimationUnit); forward-declared here so ConvertLoadOrder can call it.
static void PackageModAnimations(const Mo2Layout& mo2, const std::filesystem::path& plugins,
                                 const std::filesystem::path& stageRoot, const LogFn& log);

Result ConvertLoadOrder(const Options& opt, const LogFn& log, const std::atomic<bool>& cancel) {
    Result r;
    auto say = [&](const std::string& s) { if (log) log(s); };
    const auto convStart = std::chrono::steady_clock::now();
    auto       convT     = convStart;
    const auto phaseDone = [&](const char* what) {
        const auto now = std::chrono::steady_clock::now();
        say("  (" + std::string(what) + ": " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - convT).count()) + "s)");
        convT = now;
    };
    const fs::path dataDir = opt.dataDir, templatesDir = opt.templatesDir, outDir = opt.outputDir;

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

    CB::engine::schema::SchemaRegistry* sharedReg = CB::engine::schema::SharedRegistry();
    if (!sharedReg) { r.error = "schema registry not armed (" + CB::engine::schema::SharedRegistryError() + ")"; return r; }
    const CB::engine::schema::SchemaRegistry& schemaReg = *sharedReg;
    RosterSink rosters;

    const fs::path plugins = outDir / "community_behaviors" / "plugins";
    // MO2-PROFILE (single-bundle) mode ships one merged bundle; every leg routes into it. (Definition
    // hoisted here so the loose-derive / char-file legs above the bundle grouping can use it too.)

    if (mo2.ok) PackageModAnimations(mo2, plugins, outDir, log);
    phaseDone("mod animations");
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
    // project and universally A-poses (the SkyParkour first-person case).
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
    std::shared_ptr<CB::engine::codec::HkyArchive> baseArc;
    {
        const fs::path baseHky = dataDir / "community_behaviors" / "plugins" / "Skyrim.hky";
        std::string herr;
        baseArc = CB::engine::codec::HkyArchive::LoadFromFile(baseHky.string(), herr);
        if (!baseArc) { r.error = "base bundle not readable at " + baseHky.string() + " (install Community Behaviors first): " + herr; return r; }
    }
    CB::converter::BaseUnitCompiler baseCompiler(baseArc);

    // Base-derived resolution maps (ground truth = Skyrim.hky). Defined here (before the loose legs +
    // the per-bundle loop + the animationnames post-pass) so every roster leg routes by real actor/
    // character. See BaseMaps.h.
    CB::converter::BaseMaps baseMaps;
    if (baseArc) baseMaps = CB::converter::BuildBaseMaps(*baseArc);

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
    /// @brief What one graph conversion did — a delta written, skipped (with its log line), or nothing to emit.
    struct GraphOutcome { bool delta = false; bool skipped = false; std::string note; };
    /// @note Thread-safe by construction: it reads only read-only state (the warmed template caches, the master tables,
    ///       the base compiler — which locks its own cache), writes only `outUnit`, and REPORTS its outcome instead of
    ///       touching shared counters or the log, so a bundle's graphs can convert in parallel.
    /// @brief A bundle's master table, or an empty one — a non-inserting lookup (safe from parallel graph converts).
    const auto tableOf = [&](const std::string& bundleLower) -> const std::vector<std::string>& {
        static const std::vector<std::string> kEmpty;
        const auto it = masterTableOf.find(bundleLower);
        return it == masterTableOf.end() ? kEmpty : it->second;
    };
    auto convertGraph = [&](const std::string& g, bool firstPerson, const std::string& bname,
                            const std::vector<std::string>& masters, const std::vector<std::string>& masterPatchDirs,
                            const std::vector<std::string>& patchDirs, const std::string& outUnit) -> GraphOutcome {
        const std::string label = bname + "/" + (firstPerson ? "_1stperson/" : "") + g;
        const auto skip = [&](const std::string& why) { return GraphOutcome{ false, true, "  " + label + ": " + why }; };
        const std::string& baseXml = firstPerson ? fpBaseXmlCache.at(g) : baseXmlCache.at(g);
        if (baseXml.empty()) return skip("no template — skipped");
        const std::string prefix = ToLower(graphServePathOf(g, firstPerson));
        CB::engine::decompile::ParsedHky base;
        std::string err;
        if (!baseCompiler.Parse(prefix, base, err)) return skip("base unit unreadable — " + err);
        const auto& bones = baseCompiler.BonesFor(prefix);
        const auto* bp = bones.empty() ? nullptr : &bones;
        for (const auto& m : masters) {
            const fs::path mu = plugins / (m + ".hky") / fs::path(prefix);
            std::error_code me;
            if (!fs::is_directory(mu, me)) continue;
            if (!CB::engine::decompile::MergeHkyPatch(base, CB::engine::common::DiskUnitSource(mu.string()), schemaReg,
                                                    { "skyrim" }, tableOf(ToLower(m)), err, bp))
                return skip("master '" + m + "' patch doesn't merge — " + err);
        }
        const auto md = CB::engine::decompile::ConvertModDelta(baseXml, masterPatchDirs, patchDirs, base, { "skyrim" },
                                                             tableOf(ToLower(bname)), schemaReg, outUnit, bp);
        if (!md.ok) return skip("patch FAILED — " + md.error);
        return GraphOutcome{ md.patched != 0 || md.added != 0, false, {} };
    };

    // The converter no longer emits its OWN Skyrim.hky base. The full-corpus vanilla master
    // ships with Community Behaviors (Data/Community Behaviors/community_behaviors/plugins/Skyrim.hky, built
    // by `--regen-master`) and is THE one base every mod .hky layers over at runtime. Keeping the
    // converter out of the master business means (a) there is exactly ONE canonical Skyrim.hky —
    // a converter-regenerated one could subtly drift from the shipped one (different decompile
    // path, index base), and (b) no two mods write community_behaviors/plugins/Skyrim.hky into the
    // MO2 VFS (that collision is a known crash). Mod behavior deltas align on the vanilla
    // TEMPLATES and derive against the shipped master's own units (their FormIDs). Only the
    // "Skyrim" references in loadorder.txt + the mod manifests remain (they name the base bundle,
    // which is installed separately with Community Behaviors).
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
        const fs::path cfBundle = plugins / "CharacterFiles.hky";
        for (const auto& rel : charTemplates) {
            std::error_code we;
            const fs::path winner = dataDir / "meshes" / fs::path(rel);
            const fs::path tmpl   = templatesDir / "characters" / fs::path(rel);
            if (!fs::is_regular_file(winner, we)) continue;
            // fast path: identical size -> almost certainly vanilla; skip the decompile
            if (fs::file_size(winner, we) == fs::file_size(tmpl, we)) continue;

            std::vector<std::uint8_t> bytes;
            std::string rerr;
            if (!CB::engine::codec::ReadHavokFile(winner.string(), bytes, &rerr)) {
                ++r.skipped; say("  " + rel + ": loose-file READ FAILED — " + rerr); continue;
            }
            std::vector<std::string> modded;
            if (!WithFirstOfClass(bytes, schemaReg, "hkbCharacterStringData",
                                  [&](CB::engine::codec::io::SchemaObject& so) { modded = so.FieldRef("animationNames").strs; })) {
                ++r.skipped; say("  " + rel + ": loose-file is not a readable character"); continue;
            }
            std::unordered_set<std::string> have;
            if (const auto src = baseArc->source(ToLower("meshes/" + fs::path(rel).generic_string())))
                if (const auto cs = CB::engine::decompile::ReadCharacterStrings(*src))
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
                if (u.kind != CB::engine::codec::HkyArchive::UnitKind::Behavior) continue;
                if (u.prefix.rfind("meshes/actors/character/", 0) == 0) continue;   // Nemesis-owned
                std::error_code we;
                const fs::path winner = dataDir / fs::path(u.prefix);               // VFS-winning file
                if (!fs::is_regular_file(winner, we)) continue;                      // graph not present

                // Owning mod for this precompiled prefix (top-priority shipper), else anonymous.
                const auto pm = prefixToMod.find(ToLower(fs::path(u.prefix).generic_string()));
                const bool        attributed = pm != prefixToMod.end();
                const std::string bname      = attributed ? pm->second : std::string("BehaviorFiles");
                const fs::path    outBundle  = plugins / (bname + ".hky");

                /// @note Derived against the base UNIT (its own FormIDs), so matched overrides land on the served
                ///       base's nodes by construction — no vanilla binary or numbering oracle involved.
                const fs::path unit = outBundle / fs::path(u.prefix);
                std::vector<std::uint8_t> mb;
                std::string derr;
                CB::engine::decompile::ParsedHky base;
                CB::engine::decompile::HkyPatchStats st;
                const auto& bones = baseCompiler.BonesFor(u.prefix);
                if (!CB::engine::codec::ReadHavokFile(winner.string(), mb, &derr) || !baseCompiler.Parse(u.prefix, base, derr) ||
                    !CB::engine::decompile::DeriveHkyPatch(base, { "skyrim" }, { ToLower(bname), "skyrim" }, mb, schemaReg,
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
                    const std::string actorPath = CB::converter::ActorOfServePath(ToLower(u.prefix));
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

    auto codeDirOf = [&](const std::string& c) { return dataDir / "Nemesis_Engine" / "mod" / c; };

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

    // Read every template once, up front: the parallel graph converts below only READ these caches.
    for (const auto& g : graphs)   baseXmlOf(g);
    for (const auto& g : fpGraphs) fpBaseXmlOf(g);

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

        // 2a) behavior graph deltas — MERGE every owning code's patch dir for a graph in ONE convert; 2a') the same for
        //     FIRST-PERSON graphs against the first-person base. Each graph is independent (its own unit; masters were
        //     converted earlier), so the bundle's graphs convert in parallel and their outcomes fold in order.
        struct GraphJob {
            std::string              g;
            bool                     firstPerson = false;
            std::vector<std::string> masterDirs, patchDirs;
            std::string              outUnit;
        };
        std::vector<GraphJob> jobs;
        for (const auto& g : graphs) {
            std::vector<std::string> patchDirs;
            for (const auto& c : codeList) {
                const fs::path patch = codeDirOf(c) / g;
                if (fs::is_directory(patch, ec)) patchDirs.push_back(patch.string());
            }
            if (!patchDirs.empty()) jobs.push_back({ g, false, masterDirsOf(g), std::move(patchDirs), unitOut(bname, g) });
        }
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
            if (!patchDirs.empty())
                jobs.push_back({ g, true, masterDirsOf(fs::path("_1stperson") / g), std::move(patchDirs), fpUnitOut(bname, g) });
        }
        std::vector<GraphOutcome> outcomes(jobs.size());
        ParallelFor(jobs.size(), [&](std::size_t i) {
            const auto& j = jobs[i];
            outcomes[i] = convertGraph(j.g, j.firstPerson, bname, masters, j.masterDirs, j.patchDirs, j.outUnit);
        });
        int n = 0;
        for (const auto& o : outcomes) {
            if (o.skipped) { ++r.skipped; say(o.note); }
            if (o.delta) { ++r.deltas; ++n; }
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
                        if (CB::engine::codec::ReadHavokFile(hkx.string(), ub, &uerr) &&
                            CB::engine::decompile::DecompileBehaviorReflective(ub, schemaReg, unitDir.string(), uerr,
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
            const fs::path fnisBundle = plugins / "FNIS.hky";
            auto fnisResult = CommunityBehaviors::fnis::ConvertFnis(
                { animDir }, "character", { "defaultmale", "defaultfemale" }, fnisBundle,
                [&](const std::string& s) { say("  " + s); });
            if (fnisResult.ok && fnisResult.animCount > 0) {
                r.fnisAnims  = static_cast<int>(fnisResult.animCount);
                r.fnisEvents = static_cast<int>(fnisResult.eventCount);
                emitted.push_back("FNIS");
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
    }

    // 5) Manifests — one manifest.json per bundle (BundleManifest schema). Every mod bundle
    //    declares "Skyrim" as its master so BR can tell an override of a vanilla node from a
    //    net-new local state; the master itself (Skyrim.hky) ships with BR and carries its own
    //    manifest, so the converter never writes one (that would recreate a phantom Skyrim.hky
    //    here to collide with the shipped one). Per-mod identity is lifted from the Nemesis
    //    mod's info.ini when it ships one.
    phaseDone("mod bundles + loose legs + FNIS");
    say("");
    say("== Manifests ==");
    int manifests = 0;
    if (charFilesAny) {
        WriteManifest(plugins / "CharacterFiles.hky", "CharacterFiles", "", "", { "Skyrim" });
        ++manifests;
    }
    if (behFilesAny) {
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
    phaseDone("manifests");
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
            /// @note A pack that skipped unreadable files is incomplete — keep the unpacked bundle rather than
            ///       replace it with an archive missing those files.
            if (CB::converter::platform::ZipDir(dir.string(), tmp.string(), zerr) && zerr.empty()) {
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
    phaseDone("pack bundles");
    say("  (total: " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - convStart).count()) + "s)");
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
    const auto dc = CB::engine::anim::DecompileAnimation(abytes, bakeTmp);
    if (!dc.ok) { reason = dc.error; return AnimBakeOutcome::Skip; }   // incl. "not spline-compressed"

    if (!poseGate) {
        fs::create_directories(unitPath.parent_path(), ec);
        std::ifstream src(bakeTmp / "animation.yaml", std::ios::binary);
        std::ofstream dst(unitPath, std::ios::binary | std::ios::trunc);
        dst << src.rdbuf();
        return AnimBakeOutcome::Ok;
    }
    try {
        const auto ref = CB::engine::anim::AnimationYamlLoader::Load(bakeTmp / "animation.yaml");
        const auto rc  = CB::engine::anim::CompileAnimation(ref, 30);   // fps ignored: ref carries numFrames
        if (!rc.ok) { reason = "recompile: " + rc.error; return AnimBakeOutcome::Skip; }

        // pose gate: re-decompile the recompiled bytes and compare rotations to `ref`.
        const fs::path animRt = stageDir / "anim_rt"; fs::remove_all(animRt, ec); fs::create_directories(animRt, ec);
        if (!CB::engine::anim::DecompileAnimation(rc.bytes, animRt).ok) { reason = "re-decompile"; return AnimBakeOutcome::Skip; }
        const auto rt = CB::engine::anim::AnimationYamlLoader::Load(animRt / "animation.yaml");
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
                                const std::string& keepStagingDir) {
    namespace fs = std::filesystem;
    const auto say = [&](const std::string& s) { if (log) log(s); };
    BaseBuildResult r;
    std::error_code ec;

    const fs::path meshes(vanillaMeshesDir);
    if (!fs::is_directory(meshes, ec)) { r.error = "vanilla meshes folder not found: " + vanillaMeshesDir; return r; }

    /// @note Stage in a PRIVATE temp root (per process) so the deep unit tree stays under MAX_PATH and nothing else
    ///       can write into it mid-build; only after packing is the unpacked <Name>.hky/ tree copied to
    ///       keepStagingDir (when set). Staging straight into the shared SCT_BASE_MASTER_DIR let another project's
    ///       regen (Skyrim Content Tools, same env var) wipe and rewrite units under a running CB pack — 34 units of
    ///       a deployed Skyrim.hky came out in the retired format. The bundle's name IS the output's filename.
    const std::string bundle    = fs::path(outHky).filename().string();
    const bool        keepStage = !keepStagingDir.empty();
    /// @note With a keep root, stage on ITS volume (still a private, per-process folder) so keeping the unpacked tree is
    ///       a rename — staging in %TEMP% on another drive made it a ~77k-file copy.
    const fs::path    stage     = (keepStage ? fs::path(keepStagingDir) : fs::temp_directory_path(ec)) /
                                  ((keepStage ? ".cb_base_build_" : "cb_base_build_") + fs::path(bundle).stem().string() +
                                   "_" + std::to_string(_getpid()));
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

    auto* sreg = CB::engine::schema::SharedRegistry();
    if (!sreg) { r.error = "schema registry not armed"; return r; }

    /// @note RIG BINDING before any emit: a graph's bone-named fields render through the rig its characters bind
    ///       (project characterFilenames -> character rigName / behaviorFilename), read from the source binaries —
    ///       the same binding the runtime rebuilds from the emitted units (Resolver / BaseUnitCompiler).
    const auto phaseStart = std::chrono::steady_clock::now();
    auto       phaseT     = phaseStart;
    const auto phaseDone  = [&](const char* what) {
        const auto now = std::chrono::steady_clock::now();
        say("  (" + std::string(what) + ": " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - phaseT).count()) + "s)");
        phaseT = now;
    };

    /// @brief One source .hkx of a behavior-system kind, found by the single tree walk below.
    struct SourceFile { fs::path path; std::string rel, lower; HkxKind kind; };
    std::vector<SourceFile> sources;

    CB::engine::skeleton::RigIndex                    rigIndex;
    std::map<std::string, std::vector<std::string>> rigBones;   // skeleton serve key -> bone names (original case)
    {
        /// @note The walk only LISTS (.hkx by extension, path made relative lexically — fs::relative canonicalizes
        ///       against the disk, and ran for all ~43k files); the head peeks and the skeleton / project / character
        ///       reads then fan out, and every map is assembled afterwards in walk order.
        struct Listed { fs::path path; std::string rel, key; };
        std::vector<Listed> listed;
        for (fs::recursive_directory_iterator it(meshes, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code fe;
            if (!it->is_regular_file(fe) || fe) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".hkx") continue;
            std::string rel = it->path().lexically_relative(meshes).generic_string();
            std::string key = "meshes/" + rel;
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            listed.push_back({ it->path(), std::move(rel), std::move(key) });
        }

        std::vector<HkxKind>                                listedKind(listed.size(), HkxKind::Other);
        std::vector<std::optional<std::vector<std::string>>> skelBones(listed.size());
        ParallelFor(listed.size(), [&](std::size_t i) {
            listedKind[i] = PeekHkxKind(listed[i].path);
            if (listedKind[i] != HkxKind::Skeleton) return;
            std::vector<std::uint8_t> b; std::vector<CB::engine::skeleton::SkeletonData> sk;
            if (CB::engine::codec::ReadHavokFile(listed[i].path.string(), b, nullptr) &&
                CB::engine::skeleton::LoadSkeletonsFromHkx(b.data(), b.size(), sk, nullptr) && !sk.empty()) {
                auto& names = skelBones[i].emplace();
                for (const auto& bone : sk[0].bones) names.push_back(bone.name);
            }
        });

        std::map<std::string, fs::path> projects, characters;   // serve key -> source file
        for (std::size_t i = 0; i < listed.size(); ++i) {
            const HkxKind kind = listedKind[i];
            if (kind != HkxKind::Other) sources.push_back({ listed[i].path, listed[i].rel, listed[i].key.substr(7), kind });
            if (kind == HkxKind::Project)   projects[listed[i].key] = listed[i].path;
            if (kind == HkxKind::Character) characters[listed[i].key] = listed[i].path;
            if (kind == HkxKind::Skeleton && skelBones[i]) rigBones[listed[i].key] = std::move(*skelBones[i]);
        }

        // Every character's rig and every project's character list, read in parallel; RigIndex is then fed serially.
        std::vector<std::pair<std::string, fs::path>> charList(characters.begin(), characters.end());
        std::vector<std::optional<CB::engine::skeleton::CharacterRig>> charRig(charList.size());
        ParallelFor(charList.size(), [&](std::size_t i) {
            std::vector<std::uint8_t> cb;
            if (!CB::engine::codec::ReadHavokFile(charList[i].second.string(), cb, nullptr)) return;
            WithFirstOfClass(cb, *sreg, "hkbCharacterStringData", [&](CB::engine::codec::io::SchemaObject& so) {
                charRig[i] = CB::engine::skeleton::CharacterRig{ so.FieldRef("rigName").str, so.FieldRef("behaviorFilename").str };
            });
        });
        std::map<std::string, std::optional<CB::engine::skeleton::CharacterRig>> rigOfChar;
        for (std::size_t i = 0; i < charList.size(); ++i) rigOfChar.emplace(charList[i].first, std::move(charRig[i]));

        std::vector<std::pair<std::string, fs::path>> projList(projects.begin(), projects.end());
        std::vector<std::vector<std::string>>         projChars(projList.size());
        ParallelFor(projList.size(), [&](std::size_t i) {
            std::vector<std::uint8_t> pb;
            if (CB::engine::codec::ReadHavokFile(projList[i].second.string(), pb, nullptr))
                WithFirstOfClass(pb, *sreg, "hkbProjectStringData",
                                 [&](CB::engine::codec::io::SchemaObject& so) { projChars[i] = so.FieldRef("characterFilenames").strs; });
        });
        for (std::size_t i = 0; i < projList.size(); ++i)
            rigIndex.AddProject(projList[i].first, projChars[i],
                [&](const std::string& charUnit) -> std::optional<CB::engine::skeleton::CharacterRig> {
                    const auto c = rigOfChar.find(charUnit);
                    return c == rigOfChar.end() ? std::nullopt : c->second;
                },
                [&](const std::string& rig) { return rigBones.count(rig) != 0; });
        rigIndex.Finalize();
    }
    const auto bonesFor = [&](const std::string& serveKey) -> const std::vector<std::string>* {
        const auto* rigs = rigIndex.RigsFor(serveKey);
        if (!rigs || rigs->empty()) return nullptr;
        const auto b = rigBones.find(rigs->front());
        return b == rigBones.end() || b->second.empty() ? nullptr : &b->second;
    };

    phaseDone("scan + rig binding");
    say("== Building " + bundle + " (every unit under " + meshes.string() + ") ==");

    /// @note Every source is independent: it writes only its own unit path, the schema registry and the rig maps are
    ///       read-only from here on, and each worker bakes animations in its own scratch dir. So the corpus fans across
    ///       the pool; each file's log lines are buffered and emitted afterwards in walk order (a deterministic log).
    std::atomic<int> nBehaviors{ 0 }, nProjects{ 0 }, nCharacters{ 0 }, nSkeletons{ 0 }, nFailed{ 0 };
    std::atomic<int> nAnimOk{ 0 }, nAnimSkip{ 0 }, nAnimFail{ 0 };
    std::vector<std::string> notes(sources.size());
    const auto processOne = [&](const SourceFile& s, const fs::path& scratch, std::string& note) {
        const auto say = [&](const std::string& line) { note += line; note += '\n'; };
        const std::string& rel   = s.rel;
        const std::string& lower = s.lower;
        // Everything — behaviors, characters, projects, skeletons AND animations — bakes into the one bundle.
        const fs::path unit = stageHky / "meshes" / fs::path(rel);

        // SKELETON: hkaSkeleton -> bonelist.yaml + bones/ unit (SkeletonImport -> EmitSkeletonYamlTree).
        // The runtime builds each rig's bone table from these (Resolver m_skeletons); without one, every
        // bone-named field in that actor's graphs fails to parse.
        if (s.kind == HkxKind::Skeleton) {
            std::vector<std::uint8_t> sbytes; std::string srerr;
            std::vector<CB::engine::skeleton::SkeletonData> sk; std::string skerr;
            if (CB::engine::codec::ReadHavokFile(s.path.string(), sbytes, &srerr) &&
                CB::engine::skeleton::LoadSkeletonsFromHkx(sbytes.data(), sbytes.size(), sk, &skerr) && !sk.empty()) {
                // Attach per-bone ragdoll physics (mass/radius/capsule/joint) BEFORE emit. Without this the
                // YAML tree is anim-only, CompileSkeletonFull derives 0 ragdoll bones, and BR's SERVED
                // skeleton has no ragdoll skeleton (skels[1]) → hkbRagdollDriver can't bind → every humanoid
                // bind-poses instead of ragdolling on death.
                std::string perr;
                if (!CB::engine::skeleton::ReadSkeletonPhysics(sbytes.data(), sbytes.size(), sk[0], &perr))
                    say("  note: skeleton physics read failed for " + rel + " (" + perr + ") — anim-only (no ragdoll derive).");
                std::string eerr;
                if (CB::engine::skeleton::EmitSkeletonYamlTree(sk[0], unit, &eerr)) ++nSkeletons;
                else { ++nFailed; say("  skeleton emit FAILED: " + rel + " — " + eerr); }
            } else { ++nFailed; say("  skeleton read/parse FAILED: " + rel + " — " + (srerr.empty() ? skerr : srerr)); }
            return;
        }

        // ANIMATION — BAKE the decompiled animation.yaml INTO the master, SELF-GATED on pose fidelity: decompile, then
        // prove the round-trip (recompile, re-decompile, compare bone rotations frame-by-frame). Only a faithful
        // round-trip stays; otherwise no unit is written and the engine keeps the loose vanilla .hkx.
        if (s.kind == HkxKind::Animation) {
            std::vector<std::uint8_t> abytes; std::string arerr;
            if (!CB::engine::codec::ReadHavokFile(s.path.string(), abytes, &arerr)) {
                ++nAnimFail; say("  anim read FAILED: " + rel + " — " + arerr); return;
            }
            std::string reason;
            switch (BakeAnimationUnit(abytes, unit, scratch, reason)) {
                case AnimBakeOutcome::Ok:   ++nAnimOk; break;
                case AnimBakeOutcome::Skip:
                    ++nAnimSkip;
                    if (reason.find("not spline-compressed") == std::string::npos)   // quiet skip for those
                        say("  anim skip: " + rel + " — " + reason);
                    break;
                case AnimBakeOutcome::Fail: ++nAnimFail; say("  anim FAILED: " + rel + " — " + reason); break;
            }
            return;
        }

        /// @note BEHAVIOR / CHARACTER / PROJECT — a complete reflective unit (self-owned FormIDs, encounter order), its
        ///       bone-named fields rendered through the rig its characters bind. A mod's patch is derived against
        ///       THIS unit's identity, so no template numbering is involved.
        std::vector<std::uint8_t> bytes;
        std::string rerr;
        if (!CB::engine::codec::ReadHavokFile(s.path.string(), bytes, &rerr)) { ++nFailed; return; }
        std::string derr;
        if (!CB::engine::decompile::DecompileBehaviorReflective(bytes, *sreg, unit.string(), derr, bonesFor("meshes/" + lower))) {
            ++nFailed;
            say("  decompile FAILED: " + rel + " — " + derr);
            return;
        }
        if      (s.kind == HkxKind::Behavior)  ++nBehaviors;
        else if (s.kind == HkxKind::Character) ++nCharacters;
        else if (s.kind == HkxKind::Project)   ++nProjects;
    };
    {
        std::atomic<std::size_t> next{ 0 };
        std::vector<std::thread> pool;
        const unsigned           nThreads = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned t = 0; t < nThreads; ++t)
            pool.emplace_back([&, t] {
                const fs::path scratch = stage / ("worker" + std::to_string(t));
                for (std::size_t i; !cancel && (i = next++) < sources.size();) processOne(sources[i], scratch, notes[i]);
            });
        for (auto& th : pool) th.join();
    }
    if (cancel) { r.error = "cancelled"; return r; }
    for (const auto& n : notes)
        if (!n.empty()) say(n.substr(0, n.size() - 1));
    r.behaviors = nBehaviors; r.projects = nProjects; r.characters = nCharacters; r.skeletons = nSkeletons; r.failed = nFailed;
    r.animOk = nAnimOk; r.animSkip = nAnimSkip; r.animFail = nAnimFail;
    say("  decompiled: " + std::to_string(r.behaviors) + " behaviors, " +
        std::to_string(r.projects) + " projects, " + std::to_string(r.characters) +
        " characters, " + std::to_string(r.skeletons) + " skeletons (" +
        std::to_string(r.failed) + " failed).");
    say("  animation round-trip: " + std::to_string(r.animOk) + " ok, " +
        std::to_string(r.animFail) + " failed, " + std::to_string(r.animSkip) +
        " skipped (non-spline / undecodable).");
    phaseDone("units");

    /// @note NEITHER singlefile .txt is copied into the master — both decompose into their "<name>.txt/" FOLDER
    ///       below and the anim-data / set-data servers compose the cache from it (layouts: AnimSetDataYaml.h,
    ///       AnimDataYaml.h), flat per-project (keyed by stem).
    say("== editable caches (per-project YAML) ==");
    {
        std::ifstream sf(meshes / "animationsetdatasinglefile.txt", std::ios::binary);
        std::string   setText((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
        if (!setText.empty()) {
            try {
                auto           parsed  = CB::engine::animsetdata::ParseSingleFile(setText);
                const fs::path setRoot  = stageHky / "meshes" / "animationsetdatasinglefile.txt";
                fs::create_directories(setRoot, ec);   // index.yaml + setdata/

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
                const auto crcIndex = CB::engine::animsetdata::BuildCrcIndex(cands);
                std::size_t nResolved = 0, nResidue = 0;
                for (auto& proj : parsed.projects) {
                    CB::engine::animsetdata::ResolveSetdataPaths(proj, crcIndex);
                    for (const auto& s : proj.sets)
                        for (const auto& a : s.animations) (a.compare(0, 4, "@crc") == 0 ? nResidue : nResolved)++;
                }

                // index.yaml — the project ORDER + exact header strings (load-bearing manifest).
                const std::string idx = CB::engine::animsetdata::EmitSetdataIndexYaml(parsed);
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
                    const std::string stem = CB::engine::animsetdata::StemForHeader(proj.header);
                    if (stem.empty()) continue;
                    const std::string body = CB::engine::animsetdata::EmitSetdataUnitYaml(proj);
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

        std::ifstream af(meshes / "animationdatasinglefile.txt", std::ios::binary);
        std::string   animText((std::istreambuf_iterator<char>(af)), std::istreambuf_iterator<char>());
        if (!animText.empty()) {
            try {
                const auto     parsed  = CB::engine::animdata::ParseSingleFile(animText);
                const fs::path animRoot = stageHky / "meshes" / "animationdatasinglefile.txt";
                fs::create_directories(animRoot, ec);

                // Resolve each project's character (roster source) by actor-folder co-location over the
                // just-decomposed stageHky tree, so clips are keyed by animation name (index resolved
                // from the roster at compile) instead of a hardwired magic number. Still needed for the
                // index.yaml `character:` refs and the motion reunion below.
                const auto pcs = CB::engine::animdata::LoadProjectCharacters((stageHky / "meshes").string(),
                    [](const fs::path& unit) {
                        const auto cs = CB::engine::decompile::ReadCharacterStrings(CB::engine::common::DiskUnitSource(unit));
                        return cs ? cs->animationNames : std::vector<std::string>{};
                    },
                    [](const fs::path& unit) {
                        std::ifstream g(unit / "graph.yaml", std::ios::binary);
                        const std::string t((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
                        return CB::engine::codec::HkyArchive::KindOfGraphYaml(t) == CB::engine::codec::HkyArchive::UnitKind::Project;
                    });
                /// @note A project's character is the one its OWN asset list names ("Characters Wolf\\Wolf.hkx") — the
                ///       co-location heuristic above falls back to the first character of a shared actor folder, which
                ///       bound WolfProject to the dog (and mapped the wolf's motion records through the dog's roster).
                std::map<std::string, CB::engine::animdata::ProjectCharacter> projChar;
                for (const auto& proj : parsed.projects) {
                    const std::string stem = CB::engine::animdata::StemForProjectName(proj.name);
                    const auto pit = pcs.find(stem);
                    if (pit == pcs.end()) continue;
                    CB::engine::animdata::ProjectCharacter pc = pit->second;
                    const fs::path folder = fs::path(pc.ref).parent_path().parent_path();   // the project's folder
                    for (const auto& asset : proj.assetPaths) {
                        std::string a = asset;
                        std::replace(a.begin(), a.end(), '\\', '/');
                        if (ToLower(a).rfind("characters", 0) != 0) continue;
                        const fs::path unit = stageHky / "meshes" / folder / a;
                        const auto cs = CB::engine::decompile::ReadCharacterStrings(CB::engine::common::DiskUnitSource(unit));
                        if (cs && !cs->animationNames.empty()) pc = { (folder / a).generic_string(), cs->animationNames };
                        break;
                    }
                    projChar.emplace(stem, std::move(pc));
                }
                std::map<std::string, std::string> charRefByStem;
                for (const auto& [stem, pc] : pcs) charRefByStem.emplace(stem, pc.ref);
                for (const auto& [stem, pc] : projChar) charRefByStem[stem] = pc.ref;

                // index.yaml — project manifest incl. the per-project `character:` ref.
                const std::string idx = CB::engine::animdata::EmitAnimdataIndexYaml(parsed, charRefByStem);
                std::ofstream(animRoot / "index.yaml", std::ios::binary)
                    .write(idx.data(), static_cast<std::streamsize>(idx.size()));

                // FULL DERIVE (release item 4): the clips/ + motion/ folders are NO LONGER SHIPPED.
                // The runtime rebuilds the entire base cache from the graph + the CB-native animation
                // units (ProjectAnimData): clip generators from the base behaviours, annotation
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

                    /// @note Each behaviors folder's clip generators are read ONCE (DefaultMale and DefaultFemale share
                    ///       theirs), every graph in parallel; a project then merges its folders in the original order.
                    const auto behaviorDirsOf = [&](const auto& proj, const std::string& actorRoot) {
                        std::set<fs::path> dirs;
                        for (const auto& asset : proj.assetPaths) {
                            std::string a = asset;
                            std::replace(a.begin(), a.end(), '\\', '/');
                            if (lc(a).rfind("behaviors", 0) == 0) dirs.insert((meshes / actorRoot / a).parent_path());
                        }
                        return dirs;
                    };
                    const auto actorRootOf = [&](const std::string& ref) {
                        return fs::path(lc(ref)).parent_path().parent_path().generic_string();
                    };
                    std::map<fs::path, std::vector<std::size_t>> dirFiles;   // folder -> its graphs, iteration order
                    std::vector<fs::path>                        graphFiles;
                    for (const auto& proj : parsed.projects) {
                        if (!proj.hasAnimData || proj.motions.empty()) continue;
                        const auto pit = projChar.find(CB::engine::animdata::StemForProjectName(proj.name));
                        if (pit == projChar.end()) continue;
                        for (const auto& d : behaviorDirsOf(proj, actorRootOf(pit->second.ref))) {
                            if (dirFiles.count(d)) continue;
                            auto& idx = dirFiles[d];
                            std::error_code de;
                            for (fs::directory_iterator di(d, de), dend; !de && di != dend; di.increment(de))
                                if (lc(di->path().extension().string()) == ".hkx") {
                                    idx.push_back(graphFiles.size());
                                    graphFiles.push_back(di->path());
                                }
                        }
                    }
                    std::vector<std::vector<std::pair<std::string, std::string>>> graphClips(graphFiles.size());
                    ParallelFor(graphFiles.size(), [&](std::size_t i) {
                        std::vector<std::uint8_t> bytes;
                        if (!CB::engine::codec::ReadHavokFile(graphFiles[i].string(), bytes, nullptr)) return;
                        try {
                            CB::engine::codec::PackFileDeserializer des;
                            des.ObjectFactory = CB::engine::codec::io::MakeSchemaFactory(*sreg);
                            CB::engine::codec::BinaryReaderEx br(false, true, bytes);
                            des.DeserializePartially(br);
                            CB::engine::codec::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
                            for (const auto& o : des.ConstructAllOfClass(dr, "hkbClipGenerator"))
                                if (const auto* c = dynamic_cast<const CB::engine::codec::io::SchemaObject*>(o.get()))
                                    graphClips[i].emplace_back(SoStr(*c, "name"), SoStr(*c, "animationName"));
                        } catch (...) {}
                    });

                    /// @brief One animation to receive its motion record (first writer per animation, in project order).
                    struct ReunionJob { std::string canon; fs::path animYaml; const CB::engine::animdata::MotionRecord* record; };
                    std::vector<ReunionJob> jobs;
                    for (const auto& proj : parsed.projects) {
                        if (!proj.hasAnimData || proj.motions.empty()) continue;
                        const std::string stem = CB::engine::animdata::StemForProjectName(proj.name);
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
                        for (const auto& d : behaviorDirsOf(proj, actorRoot))
                            if (const auto df = dirFiles.find(d); df != dirFiles.end())
                                for (const std::size_t gi : df->second)
                                    for (const auto& [name, anim] : graphClips[gi]) clipAnim.emplace(name, anim);
                        std::unordered_map<std::string, const CB::engine::animdata::MotionRecord*> motionByIndex;
                        for (const auto& m : proj.motions) motionByIndex.emplace(m.animIndex, &m);
                        std::vector<std::pair<std::size_t, const CB::engine::animdata::MotionRecord*>> assign;   // roster slot -> record
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
                            const std::string canon = CB::engine::animdata::CanonicalAnimPath(actorRoot, roster[slot]);
                            const fs::path animYaml = stageHky / "meshes" / canon;   // baked anims live in the bundle
                            std::error_code fe;
                            if (!fs::exists(animYaml, fe)) { ++unbaked; continue; }   // not baked -> vanilla loose keeps its adsf motion
                            if (!handled.insert(canon).second) continue;   // the male/female duplicate: identical record
                            jobs.push_back({ canon, animYaml, &m });
                        }
                    }

                    /// @note The folds are independent (one file each, deduped above), so they run in parallel.
                    std::vector<int>         form(jobs.size(), 0);   // 1 folded, 2 explicit, 0 unreadable
                    std::vector<std::string> ferrs(jobs.size());
                    ParallelFor(jobs.size(), [&](std::size_t i) {
                        std::ifstream in(jobs[i].animYaml, std::ios::binary);
                        std::string   cur((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        in.close();
                        switch (CB::engine::anim::FinalizeUnitMotion(cur, *jobs[i].record, &ferrs[i])) {
                            case CB::engine::anim::UnitMotionForm::Folded:   form[i] = 1; break;
                            case CB::engine::anim::UnitMotionForm::Explicit: form[i] = 2; break;
                            default: return;
                        }
                        std::ofstream(jobs[i].animYaml, std::ios::binary | std::ios::trunc) << cur;
                    });
                    for (std::size_t i = 0; i < jobs.size(); ++i) {
                        if (form[i] == 0) { say("  motion reunion: " + jobs[i].canon + " unreadable — " + ferrs[i]); continue; }
                        ++(form[i] == 1 ? folded : kept);
                        ++reunited;
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

    phaseDone("editable caches + motion reunion");
    say("  packing -> " + outHky + " ...");
    std::string zerr;
    /// @note A skipped (unreadable) file means an incomplete master — fail rather than ship it.
    if (!CB::converter::platform::ZipDir(stageHky.string(), outHky, zerr) || !zerr.empty()) {
        r.error = "pack failed: " + zerr;
        return r;
    }
    phaseDone("pack");

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
    if (keepStage) phaseDone("keep unpacked tree");
    fs::remove_all(stage, ec);   // the private staging root never outlives the build
    say("  built: " + outHky);
    say("  (total: " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - phaseStart).count()) + "s)");
    r.ok = true;
    return r;
}

/// @brief Package EVERY animation each enabled mod contributes into its <modName>.hky as attributed single-file units — the per-mod sibling of BuildBaseBundle's
///   animation leg.
/// @note Sources = the mod's LOOSE meshes\ files + the archives the ENGINE loads for it via the plugin rule
///   (ResolveModArchivesFor). Loose beats any archive and a later-loaded archive beats an earlier one — the
///   engine's precedence inside one mod. INI-listed/built-in archives are base-game content: they belong to the
///   master, never a mod bundle (hence also the reserved bundle name "Skyrim").
/// @note FAIL-SAFE: a skip/fail writes no unit and the engine keeps the original file, so this can never break the
///   behavior/skeleton bundles built alongside.
/// @warning Archive-sourced units carry the archive's LOWERCASED path (BSA case is unrecoverable); loose-sourced
///   units keep their on-disk case.
static void PackageModAnimations(const Mo2Layout& mo2, const fs::path& plugins,
                                 const fs::path& stageRoot, const LogFn& log) {
    namespace bsa = CB::engine::codec::bsa;
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
    std::map<std::string, int>      skipReasons;
    std::vector<std::string>        unrecognized;    ///< .hkx under an animations/ folder that did NOT classify as an animation
    int totalOk = 0, totalSkip = 0, totalFail = 0, totalFromArchive = 0, modsWithAnims = 0;

    for (const std::string& modName : mo2.enabledTopFirst) {
        const fs::path    modRoot    = mo2.modsDir / modName;
        const std::string bundleName = modName;
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

        /// @note Each source bakes independently into its own unit path, so the mod's sources fan across the pool (a
        ///       scratch dir per worker). An archive's reader shares one stream, so its extracts take its lock. Results
        ///       land per source and fold in map order — the log and counts match a serial run.
        enum class Res { None, Ok, Skip, Fail, ExtractFail, Unrecognized };
        struct Outcome { Res res = Res::None; std::string note; };
        std::vector<std::pair<const std::string*, const Source*>> order;
        order.reserve(sources.size());
        for (const auto& [key, src] : sources) order.emplace_back(&key, &src);
        std::vector<Outcome>    outcomes(order.size());
        std::vector<std::mutex> archiveLocks(archives.size());
        ParallelFor(order.size(), [&](std::size_t i, unsigned worker) {
            const std::string& key = *order[i].first;
            const Source&      src = *order[i].second;
            Outcome&           out = outcomes[i];
            std::vector<std::uint8_t> bytes;
            std::string err;
            if (src.entry) {
                bool extracted;
                { std::lock_guard lk(archiveLocks[src.archive]); extracted = archives[src.archive].Extract(*src.entry, bytes, &err); }
                if (!extracted) { out = { Res::ExtractFail, err }; return; }
                if (PeekHkxKind(bytes) != HkxKind::Animation) {
                    if (key.find("/animations/") != std::string::npos) out.res = Res::Unrecognized;
                    return;
                }
            } else {
                if (PeekHkxKind(src.loose) != HkxKind::Animation) {
                    if (key.find("/animations/") != std::string::npos) out.res = Res::Unrecognized;
                    return;
                }
                if (!CB::engine::codec::ReadHavokFile(src.loose, bytes, &err)) { out.res = Res::Fail; return; }
            }
            std::string reason;
            switch (BakeAnimationUnit(bytes, bundle / fs::path(src.rel), stage / ("worker" + std::to_string(worker)), reason,
                                      /*poseGate=*/false)) {
                case AnimBakeOutcome::Ok:   out.res = Res::Ok; break;
                case AnimBakeOutcome::Skip: out = { Res::Skip, reason }; break;
                case AnimBakeOutcome::Fail: out.res = Res::Fail; break;
            }
        });

        int modOk = 0, modSkip = 0, modFail = 0, modFromArchive = 0;
        for (std::size_t i = 0; i < order.size(); ++i) {
            const Source& src = *order[i].second;
            const auto&   o   = outcomes[i];
            switch (o.res) {
                case Res::Ok:           ++modOk; if (src.entry) ++modFromArchive; break;
                case Res::Skip:         ++modSkip; ++skipReasons[o.note.substr(0, std::min<std::size_t>(o.note.find(':'), 60))]; break;
                case Res::Fail:         ++modFail; break;
                case Res::ExtractFail:  say("  extract failed: " + src.rel + " — " + o.note); ++modFail; break;
                case Res::Unrecognized: unrecognized.push_back(modName + ": " + src.rel); break;
                case Res::None:         break;
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
    PackageModAnimations(mo2, pluginsDir, pluginsDir, log);
    return true;
}

bool SerializerRoundTrip(const std::string& meshesDir, const std::string& outRoot, const LogFn& log) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    auto* reg = CB::engine::schema::SharedRegistry();
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
        if (!CB::engine::codec::ReadHavokFile(it->path().string(), src, &err) ||
            !CB::engine::codec::io::RoundtripHkx(src, *reg, canon, err)) { ++fails; say("  FAIL " + it->path().string() + " — " + err); ++n; continue; }
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
        const bool okA = CB::engine::decompile::DecompileBehaviorReflective(src,   *reg, (d / "src").string(),   e1);
        const bool okB = CB::engine::decompile::DecompileBehaviorReflective(canon, *reg, (d / "canon").string(), e2);
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
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { say("multilayer-roundtrip: schema registry not armed"); return false; }
    std::optional<CB::converter::BaseUnitCompiler> rigs;
    if (!baseHky.empty()) {
        std::string herr;
        if (auto arc = CB::engine::codec::HkyArchive::LoadFromFile(baseHky, herr)) rigs.emplace(arc);
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
        std::vector<CB::engine::decompile::MultiLayerMod> mods;
        for (const auto& [code, dir] : codes) {
            const fs::path gd = dir / g;
            std::error_code e3;
            if (!fs::is_directory(gd, e3)) continue;
            CB::engine::decompile::MultiLayerMod m{ code, {} };
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
        const auto r = CB::engine::decompile::MultiLayerRoundTrip(xml, mods, *reg, (fs::path(workRoot) / g).string(), bones);
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
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { say("hky-merge: schema registry not armed"); return false; }
    std::optional<CB::converter::BaseUnitCompiler> rigs;
    std::string err;
    if (auto arc = CB::engine::codec::HkyArchive::LoadFromFile(baseHky, err)) rigs.emplace(arc);
    std::vector<std::uint8_t> bytes;
    if (!CB::engine::codec::ReadHavokFile((fs::path(meshesDir) / rel).string(), bytes, &err)) { say("hky-merge: " + err); return false; }
    std::string serveKey = "meshes/" + fs::path(rel).generic_string();
    for (char& ch : serveKey) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));   // internal RigIndex key
    const std::vector<std::string>* bones = nullptr;
    if (rigs) if (const auto& b = rigs->BonesFor(serveKey); !b.empty()) bones = &b;
    if (!CB::engine::decompile::MergeHkyUnits(bytes, patchDirs, *reg, outDir, err, bones)) { say("hky-merge: " + err); return false; }
    say("hky-merge: " + std::to_string(patchDirs.size()) + " unit(s) merged onto " + rel + (bones ? "" : " (no rig)") + " -> " + outDir);
    return true;
}

bool PatchRoundTripAll(const std::string& vanillaMeshes, const std::string& moddedMeshes, const std::string& workRoot,
                       const LogFn& log, const std::string& baseHky) {
    const auto say = [&](const std::string& s) { if (log) log(s); };
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { say("patch-roundtrip: schema registry not armed"); return false; }
    std::optional<CB::converter::BaseUnitCompiler> rigs;
    if (!baseHky.empty()) {
        std::string herr;
        if (auto arc = CB::engine::codec::HkyArchive::LoadFromFile(baseHky, herr)) rigs.emplace(arc);
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
        if (!CB::engine::codec::ReadHavokFile(v->second.string(), vb, &err) || !CB::engine::codec::ReadHavokFile(it->path().string(), mb, &err)) { ++fails; continue; }
        const std::vector<std::string>* bones = nullptr;
        if (rigs) if (const auto& b = rigs->BonesFor("meshes/" + key); !b.empty()) bones = &b;
        const auto r = CB::engine::decompile::PatchRoundTrip(vb, mb, *reg, (fs::path(workRoot) / std::to_string(n)).string(), bones);
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
    struct Row { std::string rel; CB::engine::anim::FoldCheck c; std::string skeleton; };
    std::vector<std::optional<Row>> rows(files.size());
    std::atomic<std::size_t> next{ 0 }, failed{ 0 };
    auto work = [&] {
        for (std::size_t i; (i = next++) < files.size();) {
            std::ifstream in(files[i], std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (text.find("\n  motion:") == std::string::npos) continue;
            try {
                const auto def = CB::engine::anim::AnimationYamlLoader::LoadFromString(text, files[i].string());
                if (const auto c = CB::engine::anim::CheckFoldRoundTrip(def, 0))
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
            if (CB::engine::anim::FoldRoundTrips(c)) ++cleanPass;
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
    using CB::engine::codec::io::SchemaObject;
    auto say = [&](const std::string& m) { if (log) log(m); };
    const auto* reg = CB::engine::schema::SharedRegistry();
    std::string err;
    const auto arc = reg ? CB::engine::codec::HkyArchive::LoadFromFile(hkyPath, err) : nullptr;
    if (!arc) { say("adsf-project: " + (reg ? err : std::string("schema registry not armed"))); return false; }
    const auto index = arc->file("meshes/animationdatasinglefile.txt/index.yaml");
    if (!index) { say("adsf-project: no animationdatasinglefile.txt/index.yaml in " + hkyPath); return false; }
    const auto headers = CB::engine::animdata::ParseAnimdataIndexYaml(*index, err);

    CB::converter::BaseUnitCompiler units(arc);
    std::mutex mx;
    std::map<std::string, std::vector<CB::engine::animdata::DeriveClipInput>> clipCache;
    std::unordered_map<std::string, std::optional<CB::engine::anim::AnimationDef>> anims;
    CB::engine::animdata::ProjectionSource src;
    src.roster = [&](const std::string& key) -> std::vector<std::string> {
        const auto s = arc->source(key);
        const auto cs = s ? CB::engine::decompile::ReadCharacterStrings(*s) : std::nullopt;
        return cs ? cs->animationNames : std::vector<std::string>{};
    };
    src.clips = [&](const std::string& key) {
        if (const auto it = clipCache.find(key); it != clipCache.end()) return it->second;
        CB::engine::decompile::ParsedHky g;
        std::string e;
        std::vector<CB::engine::animdata::DeriveClipInput> c;
        if (units.Parse(key, g, e)) c = CB::engine::compile::DeriveClipInputsFromGraph(g.root);
        return clipCache[key] = c;
    };
    src.behaviorUnits = [&] {
        std::vector<std::string> out;
        for (const auto& u : arc->units())
            if (u.kind == CB::engine::codec::HkyArchive::UnitKind::Behavior) out.push_back(u.prefix);
        return out;
    };
    src.prefetch = [&](const std::vector<std::string>& paths) {
        std::vector<std::optional<CB::engine::anim::AnimationDef>> parsed(paths.size());
        std::atomic<std::size_t> next{ 0 };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t)
            pool.emplace_back([&] {
                for (std::size_t i; (i = next++) < paths.size();)
                    if (const auto text = arc->file(paths[i])) {
                        try {
                            auto def = CB::engine::anim::AnimationYamlLoader::LoadFromString(*text, paths[i]);
                            def.motion = CB::engine::anim::ResolveRootMotion(def);
                            parsed[i] = std::move(def);
                        } catch (...) {}
                    }
            });
        for (auto& t : pool) t.join();
        for (std::size_t i = 0; i < paths.size(); ++i) anims[paths[i]] = std::move(parsed[i]);
    };
    src.animation = [&](const std::string& path) -> const CB::engine::anim::AnimationDef* {
        auto it = anims.find(path);
        if (it == anims.end()) {
            src.prefetch({ path });
            it = anims.find(path);
        }
        return it != anims.end() && it->second ? &*it->second : nullptr;
    };

    CB::engine::animdata::ProjectionStats st;
    const auto t0 = std::chrono::steady_clock::now();
    const auto ours = CB::engine::animdata::ProjectAnimData(*reg, headers, src, &st, err);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (!ours) { say("adsf-project: projection failed — " + err); return false; }
    std::ifstream vin(vanillaTxt, std::ios::binary);
    const std::string vtext((std::istreambuf_iterator<char>(vin)), std::istreambuf_iterator<char>());
    const auto van = CB::engine::codec::io::ReadMetaText(*reg, "AnimationDataSingleFile", vtext, err);
    if (!van) { say("adsf-project: vanilla unreadable — " + err); return false; }
    std::string outText;
    if (!CB::engine::codec::io::WriteMetaText(*ours, outText, err)) { say("adsf-project: write refused — " + err); return false; }
    if (const char* dump = std::getenv("CB_ADSF_PROJECT_DUMP")) std::ofstream(dump, std::ios::binary) << outText;

    const auto obj = [](const std::shared_ptr<CB::engine::codec::IHavokObject>& o) -> SchemaObject& { return static_cast<SchemaObject&>(*o); };
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
            CB::engine::animdata::MotionRecord a, b;
            a.translations = mv->second->FieldRef("translations").strs; a.rotations = mv->second->FieldRef("rotations").strs;
            if (mo != om.end()) { b.translations = mo->second->FieldRef("translations").strs; b.rotations = mo->second->FieldRef("rotations").strs; }
            const CB::engine::anim::EngineMotionCurve ca(a), cb(b);
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
    const auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { say("metatext-roundtrip: schema registry not armed"); return false; }
    bool all = true;
    for (const auto& path : files) {
        std::ifstream in(path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const bool setData = ToLower(path).find("animationset") != std::string::npos;
        const std::string cls = setData ? "AnimationSetDataSingleFile" : "AnimationDataSingleFile";
        std::string err, out;
        const auto root = CB::engine::codec::io::ReadMetaText(*reg, cls, text, err);
        if (!root) { say("  FAIL read " + path + " — " + err); all = false; continue; }
        if (!CB::engine::codec::io::WriteMetaText(*root, out, err)) { say("  FAIL write " + path + " — " + err); all = false; continue; }
        std::size_t diff = 0;
        while (diff < text.size() && diff < out.size() && text[diff] == out[diff]) ++diff;
        const bool same = out == text;
        /// @note Record counts vs the hand-written parsers: the schema reads the same structure, not just the same bytes.
        std::size_t projects = root->FieldRef("projects").objs.size(), records = 0, handRecords = 0, handProjects = 0;
        for (const auto& po : root->FieldRef("projects").objs) {
            auto& p = static_cast<CB::engine::codec::io::SchemaObject&>(*po);
            if (setData) for (const auto& so : p.FieldRef("sets").objs) { (void)so; ++records; }
            else records += p.FieldRef("clips").objs.size() + p.FieldRef("motions").objs.size();
        }
        try {
            if (setData) {
                const auto hand = CB::engine::animsetdata::ParseSingleFile(text);
                handProjects = hand.projects.size();
                for (const auto& p : hand.projects) handRecords += p.sets.size();
            } else {
                const auto hand = CB::engine::animdata::ParseSingleFile(text);
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
                const auto oldDef = CB::engine::anim::AnimationYamlLoader::LoadFromString(oldText, rel.string());
                auto newDef = CB::engine::anim::AnimationYamlLoader::LoadFromString(newText, rel.string());
                if (!newDef.motion && !newDef.rootMotionVisual) ++folded;
                const auto got = CB::engine::anim::ResolveRootMotion(newDef);
                const CB::engine::anim::EngineMotionCurve want(*oldDef.motion), have(got ? *got : CB::engine::animdata::MotionRecord{});
                keysOld += oldDef.motion->translations.size() + oldDef.motion->rotations.size();
                if (got) {
                    const std::size_t k = got->translations.size() + got->rotations.size();
                    keysNew += k;
                    for (std::size_t prevMax = keysMax; k > prevMax && !keysMax.compare_exchange_weak(prevMax, k);) {}
                }
                double err = 0, speed = 0;
                CB::engine::anim::MotionPose prev = want.Sample(0);
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
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { say("hky-roundtrip: schema registry not armed"); return false; }
    std::optional<CB::converter::BaseUnitCompiler> rigs;
    if (!baseHky.empty()) {
        std::string herr;
        if (auto arc = CB::engine::codec::HkyArchive::LoadFromFile(baseHky, herr)) rigs.emplace(arc);
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
        if (!CB::engine::codec::ReadHavokFile(it->path().string(), bytes, &err)) { ++fails; continue; }
        const std::string rel = fs::relative(it->path(), meshesDir, fe).generic_string();
        std::string serveKey = "meshes/" + rel;
        for (char& ch : serveKey) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));   // internal RigIndex key
        const std::vector<std::string>* bones = nullptr;
        if (rigs) if (const auto& b = rigs->BonesFor(serveKey); !b.empty()) bones = &b;
        const auto r = CB::engine::decompile::RoundTripReflective(bytes, *reg, (fs::path(workRoot) / std::to_string(n)).string(), bones);
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

RegenResult RegenerateMaster(const std::string& vanillaMeshesDir, const std::string& outHky, bool strict,
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
    r.build = BuildBaseBundle(vanillaMeshesDir, tempHky.string(), log, cancel, keepStagingDir);
    if (!r.build.ok) { r.error = "build-base failed: " + r.build.error; fs::remove_all(work, ec); return r; }

    /// @note 2) SERVED-BYTES gate: every reflective unit of the fresh bundle (behavior / character / project) must parse
    ///       back through the runtime's own reader — bones bound through its rig, as the Resolver binds them — and
    ///       serialize byte-identical to its source binary's canonical re-serialization. That is exactly what the
    ///       runtime serves for a graph no mod touches. Drifting pairs are kept for inspection.
    say("");
    say("== Validating regenerated " + bundle + " (parse + serialize every unit vs its source) ==");
    std::string herr;
    auto arc = CB::engine::codec::HkyArchive::LoadFromFile(tempHky.string(), herr);
    if (!arc) { r.error = "cannot reopen built bundle for validation: " + herr; fs::remove_all(work, ec); return r; }
    auto* sreg = CB::engine::schema::SharedRegistry();
    if (!sreg) { r.error = "schema registry not armed"; fs::remove_all(work, ec); return r; }
    CB::converter::BaseUnitCompiler baseCompiler(arc);
    const fs::path driftKeep = fs::temp_directory_path(ec) / ("cb_regen_drift_" + fs::path(outHky).stem().string());
    fs::remove_all(driftKeep, ec);
    for (const auto& u : arc->units()) {
        if (cancel) { r.error = "cancelled"; fs::remove_all(work, ec); return r; }
        if (u.kind == CB::engine::codec::HkyArchive::UnitKind::Skeleton || u.prefix.rfind("meshes/", 0) != 0) continue;
        const fs::path src = fs::path(vanillaMeshesDir) / u.prefix.substr(7);
        std::vector<std::uint8_t> srcBytes, canon, got;
        std::string e;
        if (!fs::is_regular_file(src, ec) || !CB::engine::codec::ReadHavokFile(src.string(), srcBytes, &e)) continue;
        if (!CB::engine::codec::io::RoundtripHkx(srcBytes, *sreg, canon, e)) { say("  " + u.prefix + ": source unreadable — " + e); continue; }
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

namespace {
/// @brief The spline block layout of an animation packfile — what the CB-8 gate compares.
struct SplineLayout {
    std::string                cls;   ///< the first animation's class; only hkaSplineCompressedAnimation is compared
    int                        numTransform = 0, numFloat = 0, numFrames = 0, numBlocks = 0;
    int                        maxFramesPerBlock = 0, maskAndQuantizationSize = 0;
    std::vector<std::uint32_t> blockOffsets, floatBlockOffsets;
    std::vector<std::uint8_t>  data;
};

std::optional<SplineLayout> ReadSplineLayout(const std::vector<std::uint8_t>& hkx, CB::engine::schema::SchemaRegistry& reg,
                                             std::string& err) {
    using CB::engine::codec::io::SchemaObject;
    auto so  = [](const std::shared_ptr<CB::engine::codec::IHavokObject>& o) { return std::dynamic_pointer_cast<SchemaObject>(o); };
    auto i32 = [](const std::vector<std::uint8_t>& r) { std::int32_t v = 0; if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v; };
    auto u32s = [](const std::vector<std::uint8_t>& r) {
        std::vector<std::uint32_t> v(r.size() / 4);
        if (!v.empty()) std::memcpy(v.data(), r.data(), v.size() * 4);
        return v;
    };
    try {
        CB::engine::codec::BinaryReaderEx       br(hkx);
        CB::engine::codec::PackFileDeserializer des;
        des.ObjectFactory = CB::engine::codec::io::MakeSchemaFactory(reg);
        auto root = so(des.Deserialize(br));
        if (!root) { err = "not a packfile root"; return std::nullopt; }
        for (auto& nvObj : root->FieldRef("namedVariants").objs) {
            auto nv = so(nvObj);
            if (!nv || nv->FieldRef("className").str != "hkaAnimationContainer") continue;
            auto container = so(nv->FieldRef("variant").obj);
            if (!container || container->FieldRef("animations").objs.empty()) break;
            auto a = so(container->FieldRef("animations").objs[0]);
            if (!a) break;
            SplineLayout l;
            l.cls = a->ClassName();
            if (l.cls != "hkaSplineCompressedAnimation") return l;
            l.numTransform            = i32(a->FieldRef("numberOfTransformTracks").raw);
            l.numFloat                = i32(a->FieldRef("numberOfFloatTracks").raw);
            l.numFrames               = i32(a->FieldRef("numFrames").raw);
            l.numBlocks               = i32(a->FieldRef("numBlocks").raw);
            l.maxFramesPerBlock       = i32(a->FieldRef("maxFramesPerBlock").raw);
            l.maskAndQuantizationSize = i32(a->FieldRef("maskAndQuantizationSize").raw);
            l.blockOffsets            = u32s(a->FieldRef("blockOffsets").raw);
            l.floatBlockOffsets       = u32s(a->FieldRef("floatBlockOffsets").raw);
            l.data                    = a->FieldRef("data").raw;
            return l;
        }
        err = "no hkaAnimationContainer animation";
    } catch (const std::exception& e) {
        err = e.what();
    }
    return std::nullopt;
}

/// @return where the decoder's sequential walk reaches each block's float section (relative), or nullopt if it
///         can't decode the clip.
std::optional<std::vector<std::uint32_t>> DecodedFloatStarts(const SplineLayout& l) {
    std::vector<CB::engine::codec::spline::DecodedPose> poses;
    std::vector<std::uint32_t>                          starts;
    if (!CB::engine::codec::spline::DecodeSpline(l.data.data(), l.data.size(), l.numFrames, l.numBlocks,
                                                 l.maxFramesPerBlock, l.maskAndQuantizationSize, l.blockOffsets.data(),
                                                 static_cast<int>(l.blockOffsets.size()), l.numTransform, l.numFloat,
                                                 poses, nullptr, nullptr, &starts))
        return std::nullopt;
    return starts;
}

std::string JoinU32(const std::vector<std::uint32_t>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size() && i < 8; ++i) s += (i ? " " : "") + std::to_string(v[i]);
    if (v.size() > 8) s += " ...";
    return s;
}
}  // namespace

bool AnimFloatOffsetsGate(const std::string& meshesDir, const std::string& workRoot, const LogFn& log) {
    namespace fs = std::filesystem;
    const auto say = [&](const std::string& s) { if (log) log(s); };
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { say("schema registry unavailable: " + CB::engine::schema::SharedRegistryError()); return false; }

    std::size_t scanned = 0, splines = 0, undecodable = 0, withFloat = 0;
    std::size_t vanAgree = 0, vanDisagree = 0;          ///< vanilla's stored offsets vs the decoder's walk
    std::size_t oursAgree = 0, oursDisagree = 0, recompileFail = 0, sameLayout = 0, sameLayoutMatch = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(meshesDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string rel = ToLower(it->path().lexically_relative(meshesDir).generic_string());
        if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, ".hkx") != 0 || rel.find("animations/") == std::string::npos) continue;
        ++scanned;
        std::ifstream f(it->path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string err;
        const auto van = ReadSplineLayout(bytes, *reg, err);
        if (!van || van->cls != "hkaSplineCompressedAnimation") continue;
        ++splines;
        if (van->numFloat > 0) ++withFloat;

        // 1) The decoder must reproduce vanilla's own offsets — that is what proves it measures what the engine reads.
        const auto vanWalk = DecodedFloatStarts(*van);
        if (!vanWalk) { ++undecodable; continue; }
        if (*vanWalk == van->floatBlockOffsets) ++vanAgree;
        else {
            if (vanDisagree++ < 5) say("  vanilla/decoder disagree " + rel + ": stored [" + JoinU32(van->floatBlockOffsets) +
                                       "] walked [" + JoinU32(*vanWalk) + "]");
            continue;
        }

        // 2) Recompile; our stored offsets must equal the decoder's walk over OUR bytes.
        const fs::path work = fs::path(workRoot) / "clip";
        fs::remove_all(work, ec);
        fs::create_directories(work, ec);
        const auto dec = CB::engine::anim::DecompileAnimation(bytes, work);
        if (!dec.ok) { if (recompileFail++ < 5) say("  recompile skipped " + rel + " — decompile: " + dec.error); continue; }
        std::ifstream yf(work / "animation.yaml", std::ios::binary);
        const std::string yaml((std::istreambuf_iterator<char>(yf)), std::istreambuf_iterator<char>());
        const auto comp = CB::engine::anim::CompileAnimation(CB::engine::anim::AnimationYamlLoader::LoadFromString(yaml, rel));
        if (!comp.ok) { if (recompileFail++ < 5) say("  recompile skipped " + rel + " — compile: " + comp.error); continue; }
        const auto ours     = ReadSplineLayout(comp.bytes, *reg, err);
        const auto oursWalk = ours ? DecodedFloatStarts(*ours) : std::nullopt;
        if (!oursWalk) { if (recompileFail++ < 5) say("  recompile skipped " + rel + " — our output does not decode"); continue; }
        if (*oursWalk == ours->floatBlockOffsets) ++oursAgree;
        else if (oursDisagree++ < 5)
            say("  FAIL " + rel + ": ours stored [" + JoinU32(ours->floatBlockOffsets) + "] walked [" + JoinU32(*oursWalk) + "]");

        // Where our compressed bytes happen to equal vanilla's, the offsets must equal vanilla's too.
        if (ours->blockOffsets == van->blockOffsets && ours->data == van->data) {
            ++sameLayout;
            if (ours->floatBlockOffsets == van->floatBlockOffsets) ++sameLayoutMatch;
        }
    }

    say("anim-float-offsets: " + std::to_string(scanned) + " animation(s) scanned, " + std::to_string(splines) +
        " spline (" + std::to_string(withFloat) + " with float tracks), " + std::to_string(undecodable) + " undecodable.");
    say("  vanilla floatBlockOffsets == decoder walk: " + std::to_string(vanAgree) + ", disagree " + std::to_string(vanDisagree));
    say("  recompiled floatBlockOffsets == decoder walk: " + std::to_string(oursAgree) + ", disagree " +
        std::to_string(oursDisagree) + ", recompile skipped " + std::to_string(recompileFail));
    say("  byte-identical to vanilla: " + std::to_string(sameLayout) + " clip(s), offsets equal on " +
        std::to_string(sameLayoutMatch));
    return vanDisagree == 0 && oursDisagree == 0 && sameLayoutMatch == sameLayout && oursAgree > 0;
}

}  // namespace CB::converter
