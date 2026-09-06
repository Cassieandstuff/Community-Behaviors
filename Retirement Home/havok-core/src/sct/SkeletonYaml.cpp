#include "havok/sct/SkeletonYaml.h"

#include <RymlInclude.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havok::sct {
namespace fs = std::filesystem;
namespace {

// ── emit helpers ────────────────────────────────────────────────────────────────
std::string dq(const std::string& s) {           // double-quote + escape
    std::string o = "\"";
    for (char c : s) { if (c == '\\' || c == '"') o += '\\'; o += c; }
    o += '"';
    return o;
}
std::string f(float v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return b; }
std::string vec(const Vector4& v) {
    return "[" + f(v.x) + ", " + f(v.y) + ", " + f(v.z) + ", " + f(v.w) + "]";
}
std::string vec3(const Vector4& v) {   // capsule endpoints — the w component is unused
    return "[" + f(v.x) + ", " + f(v.y) + ", " + f(v.z) + "]";
}
std::string quat(const Quaternion& q) {
    return "[" + f(q.x) + ", " + f(q.y) + ", " + f(q.z) + ", " + f(q.w) + "]";
}

// ── parse helpers (ryml), mirroring CharacterYamlLoader ─────────────────────────
bool hasChild(const c4::yml::ConstNodeRef& n, const char* k) {
    return n.readable() && n.is_map() && n.has_child(c4::to_csubstr(k));
}
std::string strf(const c4::yml::ConstNodeRef& n, const char* k) {
    if (!hasChild(n, k)) return {};
    auto c = n[c4::to_csubstr(k)];
    if (!c.has_val()) return {};
    std::string o; c4::from_chars(c.val(), &o); return o;
}
// Read up to 4 floats from a seq child; missing entries keep `fb`.
std::array<float, 4> arr4(const c4::yml::ConstNodeRef& n, const char* k, std::array<float, 4> fb) {
    if (!hasChild(n, k)) return fb;
    auto seq = n[c4::to_csubstr(k)];
    if (!seq.is_seq()) return fb;
    std::array<float, 4> a = fb;
    int i = 0;
    for (auto c : seq) {
        if (i >= 4) break;
        if (c.has_val()) { std::string s; c4::from_chars(c.val(), &s);
                           try { a[i] = std::stof(s); } catch (...) {} }
        ++i;
    }
    return a;
}

struct BoneDef {
    std::string                name;
    std::string                parent;   // "" = root
    QSTransform                pose{};
    bool                       hasPose = false;
    bool                       lockTranslation = false;
    std::optional<BonePhysics> physics;
};

bool readLockTranslation(const c4::yml::ConstNodeRef& n) {
    return strf(n, "lockTranslation") == "true";
}

float flt(const c4::yml::ConstNodeRef& n, const char* k) {
    const std::string s = strf(n, k);
    if (s.empty()) return 0.f;
    try { return std::stof(s); } catch (...) { return 0.f; }
}

// Parse the optional per-bone `physics:` block (mass/radius + optional joint). Everything else about a
// ragdoll body is DERIVED at compile, so only these knobs live in YAML.
std::optional<BonePhysics> readPhysics(const c4::yml::ConstNodeRef& n) {
    if (!hasChild(n, "physics")) return std::nullopt;
    auto pn = n["physics"];
    BonePhysics p;
    p.mass   = flt(pn, "mass");
    p.radius = flt(pn, "radius");
    if (hasChild(pn, "friction"))    p.friction    = flt(pn, "friction");
    if (hasChild(pn, "restitution")) p.restitution = flt(pn, "restitution");
    if (hasChild(pn, "capsule")) {
        auto cn = pn["capsule"];
        auto a  = arr4(cn, "a", {0, 0, 0, 0});
        auto b  = arr4(cn, "b", {0, 0, 0, 0});
        p.capsule = BoneCapsule{ Vector4{a[0], a[1], a[2], a[3]}, Vector4{b[0], b[1], b[2], b[3]} };
    }
    if (hasChild(pn, "joint")) {
        auto      jn = pn["joint"];
        BoneJoint j;
        if (strf(jn, "type") == "hinge") {
            j.type = BoneJoint::Type::Hinge;
            auto a = arr4(jn, "ang", {0, 0, 0, 0});
            j.angMin = a[0]; j.angMax = a[1];
        } else {
            j.type = BoneJoint::Type::Ragdoll;
            auto tw = arr4(jn, "twist", {0, 0, 0, 0});
            auto pl = arr4(jn, "plane", {0, 0, 0, 0});
            j.twistMin = tw[0]; j.twistMax = tw[1];
            j.coneMax  = flt(jn, "cone");
            j.planeMin = pl[0]; j.planeMax = pl[1];
        }
        if (hasChild(jn, "twist_axis")) { auto a = arr4(jn, "twist_axis", {0, 0, 1, 0}); j.twistAxis = Vector4{a[0], a[1], a[2], a[3]}; }
        if (hasChild(jn, "plane_axis")) { auto a = arr4(jn, "plane_axis", {1, 0, 0, 0}); j.planeAxis = Vector4{a[0], a[1], a[2], a[3]}; }
        p.joint = j;
    }
    return p;
}

// Skeleton-scope CharacterBumper block (authored, not derivable) — pos + capsule + material.
std::optional<SkeletonBumper> readBumper(const c4::yml::ConstNodeRef& n) {
    if (!hasChild(n, "bumper")) return std::nullopt;
    auto bn = n["bumper"];
    SkeletonBumper bp;
    auto p = arr4(bn, "pos", {0, 0, 0, 0});
    bp.pos = Vector4{p[0], p[1], p[2], p[3]};
    bp.radius = flt(bn, "radius");
    if (hasChild(bn, "friction"))    bp.friction    = flt(bn, "friction");
    if (hasChild(bn, "restitution")) bp.restitution = flt(bn, "restitution");
    if (hasChild(bn, "capsule")) {
        auto cn = bn["capsule"];
        auto a = arr4(cn, "a", {0, 0, 0, 0});
        auto b = arr4(cn, "b", {0, 0, 0, 0});
        bp.capsule = BoneCapsule{ Vector4{a[0], a[1], a[2], a[3]}, Vector4{b[0], b[1], b[2], b[3]} };
    }
    return bp;
}

QSTransform readPose(const c4::yml::ConstNodeRef& bone) {
    QSTransform p;
    p.rotation = Quaternion{0, 0, 0, 1};
    p.scale    = Vector4{1, 1, 1, 1};
    if (!hasChild(bone, "pose")) return p;
    auto pn = bone["pose"];
    auto t = arr4(pn, "translation", {0, 0, 0, 0});
    auto r = arr4(pn, "rotation",    {0, 0, 0, 1});
    auto s = arr4(pn, "scale",       {1, 1, 1, 1});
    p.translation = Vector4{t[0], t[1], t[2], t[3]};
    p.rotation    = Quaternion{r[0], r[1], r[2], r[3]};
    p.scale       = Vector4{s[0], s[1], s[2], s[3]};
    return p;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

// Collect bone defs + optional index list from a combined-file YAML tree.
void collectCombined(c4::yml::ConstNodeRef root, std::string& name,
                     std::vector<std::string>& indexList, std::vector<BoneDef>& bones,
                     std::optional<SkeletonBumper>& bumper) {
    name = strf(root, "name");
    bumper = readBumper(root);
    if (hasChild(root, "index") && root["index"].is_seq())
        for (auto e : root["index"]) { std::string s; if (e.has_val()) { c4::from_chars(e.val(), &s); indexList.push_back(s); } }
    if (hasChild(root, "bones") && root["bones"].is_map())
        for (auto b : root["bones"]) {
            BoneDef d;
            { std::string k; c4::from_chars(b.key(), &k); d.name = k; }
            d.parent          = strf(b, "parent");
            d.pose            = readPose(b);
            d.hasPose         = hasChild(b, "pose");
            d.lockTranslation = readLockTranslation(b);
            d.physics         = readPhysics(b);
            bones.push_back(std::move(d));
        }
}

} // namespace

static void emitBoneFields(std::ostream& o, const std::string& ind,
                           const SkeletonBoneData& b, const SkeletonData& d);   // fwd (defined below)
static void emitBumper(std::ostream& o, const SkeletonData& d);                 // fwd (defined below)

std::string EmitSkeletonYaml(const SkeletonData& d) {
    std::ostringstream o;
    o << "name: " << dq(d.name) << "\n";
    o << "index:\n";
    for (const auto& b : d.bones) o << "  - " << dq(b.name) << "\n";
    emitBumper(o, d);
    o << "bones:\n";
    for (const auto& b : d.bones) {
        o << "  " << dq(b.name) << ":\n";
        emitBoneFields(o, "    ", b, d);
    }
    return o.str();
}

// Shared per-bone field emitter (parent-by-name, lock, pose, physics) at a given indent — used by both
// the combined single-file form and the per-bone tree form.
static void emitBoneFields(std::ostream& o, const std::string& ind,
                           const SkeletonBoneData& b, const SkeletonData& d) {
    if (b.parentIndex >= 0 && b.parentIndex < static_cast<int>(d.bones.size()))
        o << ind << "parent: " << dq(d.bones[b.parentIndex].name) << "\n";
    if (b.lockTranslation) o << ind << "lockTranslation: true\n";
    o << ind << "pose:\n";
    o << ind << "  translation: " << vec(b.refPose.translation) << "\n";
    o << ind << "  rotation: "    << quat(b.refPose.rotation) << "\n";
    o << ind << "  scale: "       << vec(b.refPose.scale) << "\n";
    if (b.physics) {
        const auto& p = *b.physics;
        o << ind << "physics:\n";
        o << ind << "  mass: "   << f(p.mass)   << "\n";
        o << ind << "  radius: " << f(p.radius) << "\n";
        if (p.friction)    o << ind << "  friction: "    << f(*p.friction)    << "\n";
        if (p.restitution) o << ind << "  restitution: " << f(*p.restitution) << "\n";
        if (p.capsule)
            o << ind << "  capsule: { a: " << vec3(p.capsule->a) << ", b: " << vec3(p.capsule->b) << " }\n";
        if (p.joint) {
            const auto& j = *p.joint;
            std::string ax;   // authored frame axes (omit ⇒ derive)
            if (j.twistAxis) ax += ", twist_axis: " + vec3(*j.twistAxis);
            if (j.planeAxis) ax += ", plane_axis: " + vec3(*j.planeAxis);
            if (j.type == BoneJoint::Type::Hinge)
                o << ind << "  joint: { type: hinge, ang: [" << f(j.angMin) << ", " << f(j.angMax) << "]" << ax << " }\n";
            else
                o << ind << "  joint: { type: ragdoll, twist: [" << f(j.twistMin) << ", " << f(j.twistMax)
                  << "], cone: " << f(j.coneMax)
                  << ", plane: [" << f(j.planeMin) << ", " << f(j.planeMax) << "]" << ax << " }\n";
        }
    }
}

// Emit the skeleton-scope CharacterBumper block, if present.
static void emitBumper(std::ostream& o, const SkeletonData& d) {
    if (!d.bumper) return;
    const auto& b = *d.bumper;
    o << "bumper:\n";
    o << "  pos: "     << vec3(b.pos) << "\n";
    o << "  radius: "  << f(b.radius) << "\n";
    o << "  friction: "    << f(b.friction)    << "\n";
    o << "  restitution: " << f(b.restitution) << "\n";
    o << "  capsule: { a: " << vec3(b.capsule.a) << ", b: " << vec3(b.capsule.b) << " }\n";
}

bool EmitSkeletonYamlTree(const SkeletonData& d, const fs::path& dir, std::string* err) {
    std::error_code ec;
    fs::create_directories(dir / "bones", ec);
    if (ec) { if (err) *err = "cannot create " + (dir / "bones").string() + ": " + ec.message(); return false; }

    // bonelist.yaml — the frozen index order (the name↔index contract for vanilla clips).
    {
        std::ostringstream o;
        o << "index:\n";
        for (const auto& b : d.bones) o << "  - " << dq(b.name) << "\n";
        emitBumper(o, d);   // skeleton-scope authored bumper rides in the index file
        const fs::path fp = dir / "bonelist.yaml";
        std::ofstream f(fp, std::ios::binary);
        if (!f) { if (err) *err = "cannot write " + fp.string(); return false; }
        const std::string s = o.str();
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
    // bones/<name>.yaml — one file per bone (stem == bone name).
    for (const auto& b : d.bones) {
        std::ostringstream o;
        emitBoneFields(o, "", b, d);
        const fs::path fp = dir / "bones" / (b.name + ".yaml");
        std::ofstream f(fp, std::ios::binary);
        if (!f) { if (err) *err = "cannot write " + fp.string(); return false; }
        const std::string s = o.str();
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
    return true;
}

// Assemble the final SkeletonData from parsed {name, index order, bones, bumper}: Stage-1 index
// assignment (locked prefix + topo append + cycle/unknown-ref detection) + the SkeletonBoneData
// build. Shared by the file-based LoadSkeletonYaml and the in-memory *FromTexts loaders (the
// packed-.hky runtime serve, which vends each YAML unit as text, never a disk path).
static bool assembleSkeleton(const std::string& name, const std::vector<std::string>& indexList,
                             const std::vector<BoneDef>& bones,
                             const std::optional<SkeletonBumper>& bumper,
                             SkeletonData& out, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (bones.empty()) return fail("no bones found");

    std::unordered_map<std::string, const BoneDef*> byName;
    for (const auto& b : bones)
        if (!byName.emplace(b.name, &b).second) return fail("duplicate bone: " + b.name);
    std::vector<std::string>        order;
    std::unordered_set<std::string> placed;
    for (const auto& nm : indexList) {                 // locked prefix, in list order
        if (!byName.count(nm)) return fail("index lists unknown bone: " + nm);
        if (placed.insert(nm).second) order.push_back(nm);
    }
    std::vector<const BoneDef*> pending;
    for (const auto& b : bones) if (!placed.count(b.name)) pending.push_back(&b);
    bool progress = true;
    while (progress && !pending.empty()) {
        progress = false;
        std::vector<const BoneDef*> next;
        for (const auto* d : pending) {
            if (d->parent.empty() || placed.count(d->parent)) {
                order.push_back(d->name); placed.insert(d->name); progress = true;
            } else next.push_back(d);
        }
        pending.swap(next);
    }
    if (!pending.empty())
        return fail("unresolved parent or cycle at bone '" + pending[0]->name +
                    "' (parent '" + pending[0]->parent + "')");

    std::unordered_map<std::string, int> idx;
    for (int i = 0; i < static_cast<int>(order.size()); ++i) idx[order[i]] = i;

    out.name = name.empty() ? "Skeleton" : name;
    out.bumper = bumper;
    out.bones.clear();
    out.bones.reserve(order.size());
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        const BoneDef* d = byName[order[i]];
        int parentIdx = -1;
        if (!d->parent.empty()) {
            if (!byName.count(d->parent))
                return fail("bone '" + d->name + "' has unknown parent '" + d->parent + "'");
            parentIdx = idx[d->parent];
            if (parentIdx >= i)     // topo invariant: parent must precede child
                return fail("parent '" + d->parent + "' is not before child '" + d->name + "'");
        }
        SkeletonBoneData b;
        b.name = d->name; b.parentIndex = parentIdx; b.refPose = d->pose;
        b.lockTranslation = d->lockTranslation;
        b.physics = d->physics;
        out.bones.push_back(std::move(b));
    }
    return true;
}

// Parse one bone-file YAML text (a bones/<name>.yaml) into a BoneDef (name comes from the caller,
// since the file stem is the bone name). Shared by the disk + in-memory bone loaders.
static BoneDef parseBoneText(const std::string& boneName, const std::string& yamlText) {
    std::string txt = yamlText;   // parse_in_place mutates; the tree refs it — local outlives use.
    auto tree = c4::yml::parse_in_place(c4::to_substr(txt));
    auto root = tree.rootref();
    BoneDef d;
    d.name            = boneName;
    d.parent          = strf(root, "parent");
    d.pose            = readPose(root);
    d.hasPose         = hasChild(root, "pose");
    d.lockTranslation = readLockTranslation(root);
    d.physics         = readPhysics(root);
    return d;
}

bool LoadSkeletonYaml(const fs::path& path, SkeletonData& out, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };

    std::string name;
    std::vector<std::string> indexList;
    std::vector<BoneDef>     bones;
    std::optional<SkeletonBumper> bumper;

    if (fs::is_directory(path)) {
        // Skeleton-scope file: the tree emit writes bonelist.yaml (index + bumper); skeleton.yaml is the
        // legacy/optional name carrier. Read whichever exists for name/index/bumper.
        for (const char* fn : { "bonelist.yaml", "skeleton.yaml" }) {
            const fs::path sk = path / fn;
            if (!fs::exists(sk)) continue;
            std::string txt = readFile(sk);
            auto tree = c4::yml::parse_in_place(c4::to_substr(txt));
            auto root = tree.rootref();
            if (name.empty()) name = strf(root, "name");
            if (!bumper) bumper = readBumper(root);
            if (indexList.empty() && hasChild(root, "index") && root["index"].is_seq())
                for (auto e : root["index"]) { std::string s; if (e.has_val()) { c4::from_chars(e.val(), &s); indexList.push_back(s); } }
        }
        const fs::path bd = path / "bones";
        if (!fs::is_directory(bd)) return fail("no bones/ directory under " + path.string());
        std::vector<fs::path> files;
        for (auto& e : fs::directory_iterator(bd))
            if (e.is_regular_file() && e.path().extension() == ".yaml") files.push_back(e.path());
        std::sort(files.begin(), files.end());   // deterministic append order
        for (auto& fp : files)
            bones.push_back(parseBoneText(fp.stem().string(), readFile(fp)));
    } else {
        std::string txt = readFile(path);
        if (txt.empty()) return fail("cannot read " + path.string());
        auto tree = c4::yml::parse_in_place(c4::to_substr(txt));
        collectCombined(tree.rootref(), name, indexList, bones, bumper);
    }
    return assembleSkeleton(name, indexList, bones, bumper, out, err);
}

// In-memory twin of LoadSkeletonYaml's directory branch — parse a base skeleton unit vended as text
// by a packed .hky (bonelist.yaml text + [{boneName, bones/<name>.yaml text}, …]). The runtime
// serve path: read the unit's files from the bundle, hand them here, CompileSkeletonFull the result.
bool LoadSkeletonYamlFromTexts(const std::string& bonelistText,
                               const std::vector<std::pair<std::string, std::string>>& boneFiles,
                               SkeletonData& out, std::string* err) {
    std::string name;
    std::vector<std::string> indexList;
    std::vector<BoneDef>     bones;
    std::optional<SkeletonBumper> bumper;
    if (!bonelistText.empty()) {
        std::string txt = bonelistText;   // parse_in_place mutates; tree refs it — outlives the block.
        auto tree = c4::yml::parse_in_place(c4::to_substr(txt));
        auto root = tree.rootref();
        name   = strf(root, "name");
        bumper = readBumper(root);
        if (hasChild(root, "index") && root["index"].is_seq())
            for (auto e : root["index"]) { std::string s; if (e.has_val()) { c4::from_chars(e.val(), &s); indexList.push_back(s); } }
    }
    std::vector<std::pair<std::string, std::string>> sorted = boneFiles;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [bname, btext] : sorted) bones.push_back(parseBoneText(bname, btext));
    return assembleSkeleton(name, indexList, bones, bumper, out, err);
}

static SkeletonBoneAdd parseBoneAddText(const std::string& boneName, const std::string& yamlText) {
    std::string txt = yamlText;   // parse_in_place mutates; tree refs it — local outlives use.
    auto tree = c4::yml::parse_in_place(c4::to_substr(txt));
    auto root = tree.rootref();
    SkeletonBoneAdd a;
    a.name            = boneName;                // stem == the bone name (== behavior model)
    a.parent          = strf(root, "parent");
    a.pose            = readPose(root);
    a.lockTranslation = readLockTranslation(root);
    return a;
}

// Parse an ordered `index:` list of bone names out of a layer bonelist.yaml text.
static std::vector<std::string> parseOrderIndex(const std::string& yamlText) {
    std::vector<std::string> order;
    std::string txt = yamlText;
    auto tree = c4::yml::parse_in_place(c4::to_substr(txt));
    auto root = tree.rootref();
    if (root.is_map() && root.has_child("index"))
        for (auto e : root["index"]) { std::string s; if (e.has_val()) { c4::from_chars(e.val(), &s); order.push_back(std::move(s)); } }
    return order;
}

// Reorder `out` to follow `order` (the source skeleton's native bone order): names present in
// `order` come first, in that order; any leftover (not listed) keep their prior relative order and
// trail behind. No-op when `order` is empty. Load-bearing for HKX-target animations (index binding).
static void applyBoneOrder(std::vector<SkeletonBoneAdd>& out, const std::vector<std::string>& order) {
    if (order.empty()) return;
    std::unordered_map<std::string, std::size_t> rank;
    for (std::size_t i = 0; i < order.size(); ++i) rank.emplace(order[i], i);
    std::stable_sort(out.begin(), out.end(), [&](const SkeletonBoneAdd& a, const SkeletonBoneAdd& b) {
        auto ra = rank.find(a.name), rb = rank.find(b.name);
        const std::size_t ka = ra == rank.end() ? order.size() : ra->second;
        const std::size_t kb = rb == rank.end() ? order.size() : rb->second;
        return ka < kb;   // listed bones by index; unlisted (== order.size()) keep stable order, trailing
    });
}

bool LoadSkeletonLayer(const fs::path& dir, std::vector<SkeletonBoneAdd>& out, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    const fs::path bd = dir / "bones";
    if (!fs::is_directory(bd)) return fail("no bones/ directory under " + dir.string());
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(bd))
        if (e.is_regular_file() && e.path().extension() == ".yaml") files.push_back(e.path());
    std::sort(files.begin(), files.end());   // alphabetical fallback order among peers
    for (auto& fp : files) out.push_back(parseBoneAddText(fp.stem().string(), readFile(fp)));
    // Honor the source bone order if the layer records one (bonelist.yaml `index:`).
    const fs::path blf = dir / "bonelist.yaml";
    if (fs::is_regular_file(blf)) applyBoneOrder(out, parseOrderIndex(readFile(blf)));
    return true;
}

// In-memory twin — bone-add layer vended as text by a packed .hky (skeleton/<actor>/bones/<name>.yaml).
bool LoadSkeletonLayerFromTexts(const std::vector<std::pair<std::string, std::string>>& boneFiles,
                                std::vector<SkeletonBoneAdd>& out,
                                const std::string& bonelistText, std::string* /*err*/) {
    std::vector<std::pair<std::string, std::string>> sorted = boneFiles;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [bname, btext] : sorted) out.push_back(parseBoneAddText(bname, btext));
    if (!bonelistText.empty()) applyBoneOrder(out, parseOrderIndex(bonelistText));   // honor source bone order
    return true;
}

bool MergeBoneAdditions(SkeletonData& base, const std::vector<SkeletonBoneAdd>& adds, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };

    std::unordered_map<std::string, int> idx;               // name -> current index
    for (int i = 0; i < static_cast<int>(base.bones.size()); ++i) idx[base.bones[i].name] = i;

    // Additions whose name isn't already frozen in the base (existing name ⇒ base wins, skip).
    std::unordered_map<std::string, const SkeletonBoneAdd*> byName;
    std::vector<const SkeletonBoneAdd*> pending;
    for (const auto& a : adds) {
        if (idx.count(a.name)) continue;                    // extensions ADD, never rewrite vanilla
        if (!byName.emplace(a.name, &a).second) continue;   // same name twice in the layer (a
                                                            // double-collected bundle, or two mods
                                                            // adding it) ⇒ first wins, SKIP the dup —
                                                            // never abort the whole merge (that dropped
                                                            // EVERY addition, serving a bare skeleton
                                                            // whose missing bones null the ragdoll driver)
        pending.push_back(&a);
    }

    // Topo-append: place an addition once its parent is in the base OR already appended. Parent may
    // be another addition, so iterate to fixpoint.
    bool progress = true;
    while (progress && !pending.empty()) {
        progress = false;
        std::vector<const SkeletonBoneAdd*> next;
        for (const auto* a : pending) {
            const bool parentReady = a->parent.empty() || idx.count(a->parent);
            if (!parentReady) { next.push_back(a); continue; }
            SkeletonBoneData b;
            b.name            = a->name;
            b.parentIndex     = a->parent.empty() ? -1 : idx[a->parent];
            b.refPose         = a->pose;
            b.lockTranslation = a->lockTranslation;
            idx[a->name]      = static_cast<int>(base.bones.size());
            base.bones.push_back(std::move(b));
            progress = true;
        }
        pending.swap(next);
    }
    if (!pending.empty())
        return fail("added bone '" + pending[0]->name + "' has an unknown parent '" +
                    pending[0]->parent + "' (not in the base skeleton or the added set)");
    return true;
}

} // namespace havok::sct
