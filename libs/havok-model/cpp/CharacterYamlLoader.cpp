#include "havok/model/yaml/CharacterYamlLoader.h"

#include <RymlInclude.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace havok::model {
namespace fs = std::filesystem;
namespace {

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// animations.txt was extracted through C#'s XML pipeline, so entity references
// (&amp; &lt; &gt; &quot; &apos;) stand in for their literal characters; C#
// decodes them on compile. Match that so names like "Human&amp;Boar" round-trip.
std::string xmlUnescape(const std::string& s) {
    std::string o; o.reserve(s.size());
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

std::vector<std::string> splitLines(const std::string& text, bool skipHashComments) {
    std::vector<std::string> out;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (skipHashComments && t[0] == '#') continue;
        out.push_back(t);
    }
    return out;
}

// Read a REQUIRED unit-relative file, throwing the same "cannot read" error the disk
// path used to throw on an empty/absent file (a present-but-empty required yaml is as
// invalid as a missing one — parse would fail anyway).
std::string readRequired(const IUnitSource& u, const char* rel) {
    auto text = u.read(rel);
    if (!text || text->empty())
        throw std::runtime_error(std::string("CharacterYamlLoader: cannot read ") + rel);
    return *text;
}

c4::yml::Tree parseYaml(const std::string& text, const char* what, std::string& storage) {
    storage = text;
    try { return c4::yml::parse_in_place(c4::to_substr(storage)); }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("CharacterYamlLoader: parse error in ") + what + ": " + e.what());
    }
}

bool hasChild(const c4::yml::ConstNodeRef& n, const char* k) {
    return n.readable() && n.is_map() && n.has_child(c4::to_csubstr(k));
}
std::string strf(const c4::yml::ConstNodeRef& n, const char* k, const char* fb = "") {
    if (!hasChild(n, k)) return fb;
    auto c = n[c4::to_csubstr(k)];
    if (!c.has_val()) return fb;
    std::string o; c4::from_chars(c.val(), &o); return o;
}
float ff(const c4::yml::ConstNodeRef& n, const char* k, float fb = 0.f) {
    std::string s = strf(n, k, ""); if (s.empty()) return fb;
    try { return std::stof(s); } catch (...) { return fb; }
}
int fi(const c4::yml::ConstNodeRef& n, const char* k, int fb = 0) {
    std::string s = strf(n, k, ""); if (s.empty()) return fb;
    try { return std::stoi(s); } catch (...) { return fb; }
}
bool boolf(const c4::yml::ConstNodeRef& n, const char* k, bool fb = false) {
    std::string s = strf(n, k, ""); if (s.empty()) return fb;
    return s == "true" || s == "True" || s == "1";
}
std::array<float, 4> arr4(const c4::yml::ConstNodeRef& n, const char* k) {
    std::array<float, 4> a{};
    if (!hasChild(n, k)) return a;
    auto seq = n[c4::to_csubstr(k)];
    if (!seq.is_seq()) return a;
    int i = 0;
    for (auto c : seq) {
        if (i >= 4) break;
        if (!c.has_val()) { i++; continue; }
        std::string s; c4::from_chars(c.val(), &s);
        try { a[i] = std::stof(s); } catch (...) { a[i] = 0.f; }
        i++;
    }
    return a;
}
// A YAML map { "bone": "value", ... } -> ordered pairs.
std::vector<std::pair<std::string, std::string>> namedMap(const c4::yml::ConstNodeRef& n) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!n.readable() || !n.is_map()) return out;
    for (auto c : n) {
        std::string k, v;
        c4::from_chars(c.key(), &k);
        if (c.has_val()) c4::from_chars(c.val(), &v);
        out.emplace_back(std::move(k), std::move(v));
    }
    return out;
}

CharacterDef loadCharacter(const IUnitSource& u) {
    std::string store;
    auto tree = parseYaml(readRequired(u, "character.yaml"), "character.yaml", store);
    auto root = tree.rootref();
    auto c = hasChild(root, "character") ? root["character"] : root;

    CharacterDef def;
    def.name     = strf(c, "name");
    def.rig      = strf(c, "rig");
    def.ragdoll  = strf(c, "ragdoll");
    def.behavior = strf(c, "behavior");
    def.scale    = ff(c, "scale", 1.f);
    if (hasChild(c, "controller")) {
        auto ct = c["controller"];
        def.controller.capsuleHeight       = ff(ct, "capsuleHeight");
        def.controller.capsuleRadius       = ff(ct, "capsuleRadius");
        def.controller.collisionFilterInfo = fi(ct, "collisionFilterInfo");
    }
    if (hasChild(c, "model")) {
        auto m = c["model"];
        def.model.up      = arr4(m, "up");
        def.model.forward = arr4(m, "forward");
        def.model.right   = arr4(m, "right");
    }
    return def;
}

CharBoneWeightsDef loadBoneWeights(const c4::yml::ConstNodeRef& bw) {
    CharBoneWeightsDef def;
    def.count  = fi(bw, "count");
    def.values = strf(bw, "values");
    if (hasChild(bw, "bone_count")) def.boneCount = fi(bw, "bone_count");
    if (hasChild(bw, "named")) def.named = namedMap(bw["named"]);
    return def;
}

std::vector<CharPropertyDef> loadProperties(const IUnitSource& u) {
    if (!u.hasDir("properties"))
        throw std::runtime_error("CharacterYamlLoader: properties dir not found");

    // Property order: _order.txt if present (authoritative, preserves the vanilla index
    // order); else the *.yaml filenames, sorted (a hand-authored unit without _order.txt).
    std::vector<std::string> order;
    if (auto ord = u.read("properties/_order.txt")) {
        order = splitLines(*ord, /*skipHashComments*/ true);
    } else {
        for (const std::string& rel : u.listYaml("properties", /*recursive*/ false)) {
            const std::string stem = fs::path(rel).stem().string();
            if (stem == "_order") continue;
            order.push_back(stem);
        }
        std::sort(order.begin(), order.end());
    }

    std::vector<CharPropertyDef> props;
    for (const auto& name : order) {
        std::string safe = name;
        std::replace(safe.begin(), safe.end(), '+', '_');   // '+' -> '_' in filenames
        const std::string rel = "properties/" + safe + ".yaml";
        auto text = u.read(rel);
        if (!text || text->empty())
            throw std::runtime_error("CharacterYamlLoader: property file not found for '" + name + "': " + rel);

        std::string store;
        auto tree = parseYaml(*text, rel.c_str(), store);
        auto n = tree.rootref();

        CharPropertyDef p;
        p.name = strf(n, "name");
        if (p.name.empty()) p.name = name;
        p.type = strf(n, "type");
        p.role = strf(n, "role", "ROLE_DEFAULT");
        if (hasChild(n, "initial_value")) p.initialValue = static_cast<long>(fi(n, "initial_value"));
        if (hasChild(n, "bone_weights")) p.boneWeights = loadBoneWeights(n["bone_weights"]);
        props.push_back(std::move(p));
    }
    return props;
}

FootIkDef loadFootIk(const IUnitSource& u) {
    std::string store;
    auto tree = parseYaml(readRequired(u, "foot_ik.yaml"), "foot_ik.yaml", store);
    auto n = tree.rootref();
    FootIkDef d;
    if (hasChild(n, "legs") && n["legs"].is_seq()) {
        for (auto L : n["legs"]) {
            FootIkLegDef leg;
            leg.kneeAxisLS = arr4(L, "kneeAxisLS");
            leg.footEndLS  = arr4(L, "footEndLS");
            leg.footPlantedAnkleHeightMS = ff(L, "footPlantedAnkleHeightMS");
            leg.footRaisedAnkleHeightMS  = ff(L, "footRaisedAnkleHeightMS");
            leg.maxAnkleHeightMS = ff(L, "maxAnkleHeightMS");
            leg.minAnkleHeightMS = ff(L, "minAnkleHeightMS");
            leg.maxKneeAngleDegrees  = ff(L, "maxKneeAngleDegrees");
            leg.minKneeAngleDegrees  = ff(L, "minKneeAngleDegrees");
            leg.maxAnkleAngleDegrees = ff(L, "maxAnkleAngleDegrees");
            leg.hipIndex   = fi(L, "hipIndex");
            leg.kneeIndex  = fi(L, "kneeIndex");
            leg.ankleIndex = fi(L, "ankleIndex");
            d.legs.push_back(leg);
        }
    }
    d.raycastDistanceUp     = ff(n, "raycastDistanceUp");
    d.raycastDistanceDown   = ff(n, "raycastDistanceDown");
    d.originalGroundHeightMS = ff(n, "originalGroundHeightMS");
    d.verticalOffset        = ff(n, "verticalOffset");
    d.collisionFilterInfo   = fi(n, "collisionFilterInfo");
    d.forwardAlignFraction  = ff(n, "forwardAlignFraction");
    d.sidewaysAlignFraction = ff(n, "sidewaysAlignFraction");
    d.sidewaysSampleWidth   = ff(n, "sidewaysSampleWidth");
    d.lockFeetWhenPlanted   = boolf(n, "lockFeetWhenPlanted");
    d.useCharacterUpVector  = boolf(n, "useCharacterUpVector");
    d.isQuadrupedNarrow     = boolf(n, "isQuadrupedNarrow");
    return d;
}

CharMirrorDef loadMirror(const IUnitSource& u) {
    std::string store;
    auto tree = parseYaml(readRequired(u, "mirror.yaml"), "mirror.yaml", store);
    auto n = tree.rootref();
    CharMirrorDef d;
    d.mirrorAxis = arr4(n, "mirrorAxis");
    if (hasChild(n, "bonePairMap")) {
        auto bp = n["bonePairMap"];
        d.bonePairMap.count  = fi(bp, "count");
        d.bonePairMap.values = strf(bp, "values");
        if (hasChild(bp, "named")) d.bonePairMap.named = namedMap(bp["named"]);
    }
    return d;
}

// The shared skeleton asset (`character assets/skeleton.yaml`) — needed only for `named`
// bone-weight maps (a hand-authored convenience; vanilla decompile emits raw `values`).
// Resolved THROUGH the source: DiskUnitSource walks the vanilla ancestor convention, an
// in-memory source packages it unit-relative. Mirrors YamlBehaviorLoader::findAndLoadSkeleton.
std::vector<std::string> findAndLoadSkeleton(const IUnitSource& u) {
    auto text = u.read("character assets/skeleton.yaml");
    if (!text) return {};
    c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(*text));
    auto root = tree.rootref();
    std::vector<std::string> bones;
    if (root.readable() && root.has_child("bones") && root["bones"].is_seq())
        for (auto b : root["bones"]) {
            if (!b.has_val()) continue;
            std::string s; c4::from_chars(b.val(), &s); bones.push_back(std::move(s));
        }
    return bones;
}

// Union a delta layer's animations.txt additions onto `data` (dedup case-insensitive,
// base order preserved). `have` holds the lowercased names already present.
void unionAnimations(CharacterData& data, const IUnitSource& u, std::unordered_set<std::string>& have) {
    auto text = u.read("animations.txt");
    if (!text) return;   // a layer need not touch this character
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    for (auto& a : splitLines(*text, /*skipHashComments*/ false)) {
        a = xmlUnescape(a);
        if (have.insert(lower(a)).second) data.animations.push_back(std::move(a));
    }
}

} // namespace

CharacterData CharacterYamlLoader::Load(const IUnitSource& unit) {
    CharacterData data;
    data.character  = loadCharacter(unit);
    if (auto anims = unit.read("animations.txt")) {
        data.animations = splitLines(*anims, /*skipHashComments*/ false);
        for (auto& a : data.animations) a = xmlUnescape(a);
    }
    data.properties = loadProperties(unit);
    data.footIk     = loadFootIk(unit);
    data.mirror     = loadMirror(unit);
    data.boneNames  = findAndLoadSkeleton(unit);
    return data;
}

CharacterData CharacterYamlLoader::LoadMerged(const std::vector<std::shared_ptr<const IUnitSource>>& sources) {
    if (sources.empty())
        throw std::runtime_error("CharacterYamlLoader::LoadMerged: no sources");

    // Base (first, lowest priority) is the full character; later layers union their
    // animationNames additions onto it (dedup case-insensitively, base order preserved).
    CharacterData data = Load(*sources.front());

    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::unordered_set<std::string> have;
    have.reserve(data.animations.size() * 2 + 16);
    for (const auto& a : data.animations) have.insert(lower(a));

    for (std::size_t i = 1; i < sources.size(); ++i)
        if (sources[i]) unionAnimations(data, *sources[i], have);
    return data;
}

CharacterData CharacterYamlLoader::Load(const fs::path& dir) {
    if (!fs::is_directory(dir))
        throw std::runtime_error("CharacterYamlLoader: character directory not found: " + dir.string());
    return Load(DiskUnitSource(dir));
}

CharacterData CharacterYamlLoader::LoadMerged(const std::vector<std::string>& dirs) {
    if (dirs.empty())
        throw std::runtime_error("CharacterYamlLoader::LoadMerged: no directories");
    std::vector<std::shared_ptr<const IUnitSource>> sources;
    sources.reserve(dirs.size());
    for (const auto& d : dirs) sources.push_back(std::make_shared<DiskUnitSource>(fs::path(d)));
    return LoadMerged(sources);
}

} // namespace havok::model
