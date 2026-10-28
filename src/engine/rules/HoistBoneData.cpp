#include <rules/HoistBoneData.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <set>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace CB::engine::rules {

namespace {
using codec::io::SchemaObject;
using schema::FieldKind;

constexpr long kTypeInt32   = 3;
constexpr long kTypePointer = 5;
constexpr long kBindCharProp = 1;

std::shared_ptr<SchemaObject> make(const schema::SchemaRegistry& reg, const std::string& cls) {
    const schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) return nullptr;
    auto o = std::make_shared<SchemaObject>(&reg, cs);
    o->Init();
    return o;
}

/// @brief Own `o` in `hky` as an inline object (category "", as a parse leaves every inline object).
void adopt(decompile::ParsedHky& hky, const std::shared_ptr<SchemaObject>& o) {
    hky.objects.push_back(o);
    hky.identity.category[o.get()] = "";
    hky.identity.ids[o.get()]      = "~rule" + std::to_string(hky.objects.size());
}

SchemaObject* asSo(const std::shared_ptr<codec::IHavokObject>& p) { return dynamic_cast<SchemaObject*>(p.get()); }

SchemaObject* firstOfClass(decompile::ParsedHky& hky, const char* cls) {
    for (const auto& o : hky.objects) if (o && std::string(o->ClassName()) == cls) return o.get();
    return nullptr;
}

/// @brief The index of the bone-data array field of a bone set class (hkbBoneWeightArray.boneWeights /
///        hkbBoneIndexArray.boneIndices).
std::optional<std::size_t> boneArrayField(const SchemaObject& so) {
    const auto fields = so.Fields();
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i]->kind == FieldKind::ScalarArray && (fields[i]->boneWeights || fields[i]->boneList)) return i;
    return std::nullopt;
}

/// @brief An identifier-safe name: every run of characters outside [A-Za-z0-9_] becomes one '_', none leading or
///        trailing; a leading digit gets a '_' in front.
std::string identifier(const std::string& s) {
    std::string out;
    for (const char ch : s) {
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (ok) out += ch;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (!out.empty() && out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
    return out;
}

std::int64_t intOf(const std::vector<std::uint8_t>& raw, std::size_t at, std::size_t w) {
    std::int64_t x = 0;
    for (std::size_t i = 0; i < w; ++i) x |= static_cast<std::int64_t>(raw[at + i]) << (8 * i);
    if (w < 8 && ((x >> (8 * w - 1)) & 1)) x |= ~((std::int64_t(1) << (8 * w)) - 1);
    return x;
}
void putInt(std::vector<std::uint8_t>& raw, std::int64_t v, std::size_t w) {
    for (std::size_t i = 0; i < w; ++i) raw.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(v) >> (8 * i)) & 0xff));
}

// ── sites ────────────────────────────────────────────────────────────────────────────────────────────────────────────

/// @brief One `charprop` member of a graph: its bindable, the object and field that hold it, the member path, and the
///        name a new property for it starts from.
struct Site {
    SchemaObject*      bindable;
    SchemaObject*      owner;
    std::size_t        field;
    std::string        path;
    std::string        label;
};

void sitesUnder(SchemaObject& b, SchemaObject& so, const std::string& prefix, const std::string& label, std::vector<Site>& out) {
    const auto fields = so.Fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const schema::Field& f = *fields[i];
        auto& v = so.FieldAt(i);
        if (!f.charProp.empty()) { out.push_back({ &b, &so, i, prefix + f.name, label }); continue; }
        if (f.kind == FieldKind::Struct)
            if (auto* e = asSo(v.obj)) sitesUnder(b, *e, prefix + f.name + "/", label, out);
        if (f.kind == FieldKind::StructArray)
            for (std::size_t k = 0; k < v.objs.size(); ++k)
                if (auto* e = asSo(v.objs[k])) sitesUnder(b, *e, prefix + f.name + ":" + std::to_string(k) + "/", label, out);
    }
}

/// @brief Every `charprop` site of `hky`, in a walk from the root through pointers (each object once) that a parse of
///        the rule-applied unit repeats exactly: the sites the record's positions index.
std::vector<Site> sitesOf(decompile::ParsedHky& hky) {
    std::vector<Site> out;
    std::unordered_set<const SchemaObject*> seen;
    const auto walk = [&](auto&& self, SchemaObject& so, const std::string& label) -> void {
        if (!seen.insert(&so).second) return;
        std::string own = label;
        if (const auto ni = so.IndexOf("name"); ni && so.Fields()[*ni]->kind == FieldKind::String && !so.Values()[*ni].str.empty())
            own = identifier(so.Values()[*ni].str);
        if (so.IndexOf("variableBindingSet")) sitesUnder(so, so, "", own, out);
        const auto fields = so.Fields();
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto& f = *fields[i];
            auto& v = so.FieldAt(i);
            if (f.kind == FieldKind::Ptr || f.kind == FieldKind::Struct) {
                if (auto* e = asSo(v.obj)) self(self, *e, own + "_" + f.name);
                for (std::size_t k = 0; k < v.objs.size(); ++k) if (auto* e = asSo(v.objs[k])) self(self, *e, own + "_" + std::to_string(k));
            } else if (f.kind == FieldKind::PtrArray || f.kind == FieldKind::StructArray) {
                for (std::size_t k = 0; k < v.objs.size(); ++k) if (auto* e = asSo(v.objs[k])) self(self, *e, own + "_" + std::to_string(k));
            }
        }
    };
    if (hky.root) walk(walk, *hky.root, std::string());
    return out;
}

/// @brief The binding `b` holds for member path `path`, or null.
SchemaObject* bindingFor(SchemaObject& b, const std::string& path) {
    auto* set = asSo(b.FieldRef("variableBindingSet").obj);
    if (!set) return nullptr;
    for (const auto& o : set->FieldRef("bindings").objs)
        if (auto* e = asSo(o); e && e->Str("memberPath") == path) return e;
    return nullptr;
}

// ── vocabularies ─────────────────────────────────────────────────────────────────────────────────────────────────────

/// @brief A unit's character-property vocabulary: a character's (names + infos + values) or a graph's (names + infos).
struct Vocab {
    SchemaObject* owner  = nullptr;   ///< hkbCharacterData / hkbBehaviorGraphData
    SchemaObject* strings = nullptr;  ///< its stringData
    bool          character = false;

    std::vector<std::string>& names() { return strings->FieldRef("characterPropertyNames").strs; }
    std::vector<std::shared_ptr<codec::IHavokObject>>& infos() { return owner->FieldRef("characterPropertyInfos").objs; }
    SchemaObject* values() { return asSo(owner->FieldRef("characterPropertyValues").obj); }

    std::optional<std::size_t> find(const std::string& name) {
        const auto& n = names();
        const auto it = std::find(n.begin(), n.end(), name);
        return it == n.end() ? std::nullopt : std::optional<std::size_t>(static_cast<std::size_t>(it - n.begin()));
    }
    long type(std::size_t i) { auto* e = asSo(infos()[i]); return e ? static_cast<long>(e->GetInt("type")) : -1; }
    /// @brief A character property's word value, and (POINTER) the object it indexes.
    std::int64_t word(std::size_t i) {
        auto* vs = values();
        if (!vs) return 0;
        const auto& w = vs->FieldRef("wordVariableValues").objs;
        auto* e = i < w.size() ? asSo(w[i]) : nullptr;
        return e ? e->GetInt("value") : 0;
    }
    SchemaObject* pointer(std::size_t i) {
        auto* vs = values();
        if (!vs) return nullptr;
        const auto& p = vs->FieldRef("variantVariableValues").objs;
        const std::int64_t w = word(i);
        return w >= 0 && static_cast<std::size_t>(w) < p.size() ? asSo(p[static_cast<std::size_t>(w)]) : nullptr;
    }
};

std::optional<Vocab> characterVocab(decompile::ParsedHky& hky) {
    Vocab v;
    v.owner = firstOfClass(hky, "hkbCharacterData");
    v.character = true;
    if (!v.owner) return std::nullopt;
    v.strings = asSo(v.owner->FieldRef("stringData").obj);
    if (!v.strings) return std::nullopt;
    return v;
}
std::optional<Vocab> graphVocab(decompile::ParsedHky& hky) {
    Vocab v;
    v.owner = firstOfClass(hky, "hkbBehaviorGraphData");
    if (!v.owner) return std::nullopt;
    v.strings = asSo(v.owner->FieldRef("stringData").obj);
    if (!v.strings) return std::nullopt;
    return v;
}

bool addInfo(decompile::ParsedHky& hky, Vocab& v, const schema::SchemaRegistry& reg, const std::string& name, long type,
             std::string& err) {
    auto info = make(reg, "hkbVariableInfo");
    auto role = make(reg, "hkbRoleAttribute");
    if (!info || !role) { err = "no hkbVariableInfo / hkbRoleAttribute class"; return false; }
    info->FieldRef("role").obj = role;
    info->SetInt("type", type);
    adopt(hky, info);
    adopt(hky, role);
    v.infos().push_back(info);
    v.names().push_back(name);
    return true;
}

// ── content ──────────────────────────────────────────────────────────────────────────────────────────────────────────

/// @brief A site's bone data, spelled in bone names through the graph's rig: what dedupe compares and what each
///        character's value is resolved from.
struct Content {
    std::string                cls;        ///< the bone set class, or "bone"
    std::vector<std::uint8_t>  raw;        ///< the source array / index bytes (graph-rig indices)
    std::vector<std::string>   names;      ///< weights: bone of each element; indices / bone: the bones named
    std::string                key;
};

std::string hex(const std::vector<std::uint8_t>& b, std::size_t at, std::size_t w) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < w; ++i) { s += d[b[at + i] >> 4]; s += d[b[at + i] & 15]; }
    return s;
}

/// @brief The bone a graph-rig index names; past the rig, the positional token `#<index>` (no bone is named so: it
///        copies to a character on the graph's rig and drops out of any other).
std::string boneName(std::int64_t idx, const std::vector<std::string>& rig) {
    return static_cast<std::size_t>(idx) < rig.size() ? rig[static_cast<std::size_t>(idx)] : "#" + std::to_string(idx);
}

/// @brief Content `raw` of class `cls` ("bone": a `width`-byte index; a bone set: its array, weights 4-byte floats, an
///        index list `width`-byte ints) spelled through `rig`; nullopt for a "no bone" (-1) single ref.
std::optional<Content> contentFromRaw(const std::string& cls, const std::vector<std::uint8_t>& raw, const std::vector<std::string>& rig,
                                      std::size_t width) {
    Content c;
    c.cls = cls;
    c.raw = raw;
    if (cls == "bone") {
        const std::int64_t idx = intOf(raw, 0, width);
        if (idx < 0) return std::nullopt;
        c.names = { boneName(idx, rig) };
        c.key   = "bone|" + c.names[0];
        return c;
    }
    c.key = cls + "|";
    if (cls == "hkbBoneWeightArray") {
        for (std::size_t k = 0; k < raw.size() / 4; ++k) {
            c.names.push_back(boneName(static_cast<std::int64_t>(k), rig));
            c.key += c.names.back() + "=" + hex(raw, k * 4, 4) + ";";
        }
    } else {
        for (std::size_t k = 0; (k + 1) * width <= raw.size(); ++k) {
            const std::int64_t idx = intOf(raw, k * width, width);
            c.names.push_back(idx < 0 ? std::string() : boneName(idx, rig));
            c.key += (idx < 0 ? std::string("-1") : c.names.back()) + ";";
        }
    }
    return c;
}

/// @return nullopt when the member holds no bone data (nothing to move).
std::optional<Content> contentOf(const Site& s, const std::vector<std::string>& rig) {
    const schema::Field& f = *s.owner->Fields()[s.field];
    const auto& v = s.owner->Values()[s.field];
    if (f.kind == FieldKind::Scalar) return contentFromRaw("bone", v.raw, rig, static_cast<std::size_t>(schema::ScalarWidth(f.scalar)));
    auto* set = asSo(v.obj);
    const auto ai = set ? boneArrayField(*set) : std::nullopt;
    if (!ai) return std::nullopt;
    const schema::Field& af = *set->Fields()[*ai];
    return contentFromRaw(f.charProp, set->Values()[*ai].raw, rig, static_cast<std::size_t>(schema::ScalarWidth(af.scalar)));
}

/// @brief `c` as the bytes a character with rig `rig` stores: the graph rig's own bytes when the rigs agree; otherwise
///        by name — a weight bone the source does not cover takes the mask's most common weight, an index list keeps the
///        bones the rig has, a bone the rig lacks is -1.
std::vector<std::uint8_t> resolve(const Content& c, const std::vector<std::string>& graphRig, const std::vector<std::string>& rig,
                                  std::size_t indexWidth) {
    if (rig == graphRig) return c.raw;
    const auto at = [&](const std::string& n) -> std::int64_t {
        const auto it = std::find(rig.begin(), rig.end(), n);
        return it == rig.end() ? -1 : static_cast<std::int64_t>(it - rig.begin());
    };
    std::vector<std::uint8_t> out;
    if (c.cls == "bone") { putInt(out, at(c.names[0]), indexWidth); return out; }
    if (c.cls == "hkbBoneWeightArray") {
        std::map<std::uint32_t, std::size_t> freq;
        std::unordered_map<std::string, std::uint32_t> byName;
        for (std::size_t k = 0; k < c.names.size(); ++k) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, c.raw.data() + k * 4, 4);
            ++freq[bits];
            byName.emplace(c.names[k], bits);
        }
        std::uint32_t dflt = 0;
        std::size_t   best = 0;
        for (const auto& [bits, n] : freq) if (n > best) { best = n; dflt = bits; }
        for (const auto& b : rig) {
            const auto it = byName.find(b);
            const std::uint32_t bits = it == byName.end() ? dflt : it->second;
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
        }
        return out;
    }
    for (const auto& n : c.names) {
        if (n.empty()) { putInt(out, -1, indexWidth); continue; }
        if (const std::int64_t i = at(n); i >= 0) putInt(out, i, indexWidth);
    }
    return out;
}

// ── the record ───────────────────────────────────────────────────────────────────────────────────────────────────────

struct HoistRecord final : Record {
    struct SiteEdit {
        std::size_t               site = 0;          ///< position in sitesOf
        std::vector<std::uint8_t> raw;               ///< the member's (scalar) or its bone set's (array) original bytes
        bool                      addedBinding = false;
        bool                      createdSet   = false;
        std::int64_t              reboundFrom  = -1;   ///< a vanilla binding re-pointed: its original variableIndex
    };
    struct GraphEdit {
        std::string           key;
        std::size_t           propsBefore = 0;
        std::vector<SiteEdit> sites;
    };
    struct CharEdit {
        std::string key;
        std::size_t propsBefore = 0, wordsBefore = 0, variantsBefore = 0;
        bool        createdValues = false;
    };
    std::vector<GraphEdit> graphs;
    std::deque<CharEdit>   chars;   ///< a deque: the forward run keeps pointers to its entries
};

/// @brief One running character's view during the forward run: its parse, vocabulary and own rig.
struct RunChar {
    const Character*                character = nullptr;
    decompile::ParsedHky*           hky = nullptr;
    Vocab                           vocab;
    const std::vector<std::string>* rig = nullptr;
    HoistRecord::CharEdit*          edit = nullptr;
};

/// @brief Append character property `name` to a character's vocabulary: an INT32 (`cls` "bone", `bytes` its
///        `width`-byte index) or a POINTER to a new `cls` bone set holding `bytes`. `created` = the values set was made.
bool addProperty(decompile::ParsedHky& hky, Vocab& v, const schema::SchemaRegistry& reg, const std::string& name,
                 const std::string& cls, const std::vector<std::uint8_t>& bytes, std::size_t width, bool& created, std::string& err) {
    created = false;
    auto* vs = v.values();
    if (!vs) {
        auto nv = make(reg, "hkbVariableValueSet");
        if (!nv) { err = "no hkbVariableValueSet class"; return false; }
        adopt(hky, nv);
        v.owner->FieldRef("characterPropertyValues").obj = nv;
        created = true;
        vs = nv.get();
    }
    auto word = make(reg, "hkbVariableValue");
    if (!word) { err = "no hkbVariableValue class"; return false; }
    adopt(hky, word);
    auto& variants = vs->FieldRef("variantVariableValues").objs;
    if (cls == "bone") {
        word->SetInt("value", intOf(bytes, 0, width));
    } else {
        auto set = make(reg, cls);
        const auto ai = set ? boneArrayField(*set) : std::nullopt;
        if (!ai) { err = "no bone set class '" + cls + "'"; return false; }
        set->FieldAt(*ai).raw = bytes;
        adopt(hky, set);
        word->SetInt("value", static_cast<std::int64_t>(variants.size()));
        variants.push_back(set);
    }
    vs->FieldRef("wordVariableValues").objs.push_back(word);
    return addInfo(hky, v, reg, name, cls == "bone" ? kTypeInt32 : kTypePointer, err);
}

/// @brief The bytes a character property holds, comparable with resolve()'s: an INT32's word, a bone set's array.
std::optional<std::vector<std::uint8_t>> propertyBytes(RunChar& rc, std::size_t i, const std::string& cls, std::size_t indexWidth) {
    if (cls == "bone") {
        if (rc.vocab.type(i) != kTypeInt32) return std::nullopt;
        std::vector<std::uint8_t> out;
        putInt(out, rc.vocab.word(i), indexWidth);
        return out;
    }
    if (rc.vocab.type(i) != kTypePointer) return std::nullopt;
    auto* o = rc.vocab.pointer(i);
    if (!o || std::string(o->ClassName()) != cls) return std::nullopt;
    const auto ai = boneArrayField(*o);
    if (!ai) return std::nullopt;
    return o->Values()[*ai].raw;
}

}  // namespace

// ── the op ───────────────────────────────────────────────────────────────────────────────────────────────────────────

HoistBoneData::HoistBoneData(const schema::RuleDef& rule, std::string& err) : m_rule(rule) {
    if (std::find(rule.select.begin(), rule.select.end(), "charprop") == rule.select.end())
        err = "HoistBoneData moves `charprop` members: its rule must select [charprop]";
}

bool HoistBoneData::Apply(Context& ctx, std::unique_ptr<Record>& out, std::string& err) {
    const auto& reg = ctx.Registry();
    auto rec = std::make_unique<HoistRecord>();

    /// @note Every graph some character runs, with its runners in Characters() order.
    std::map<std::string, std::vector<const Character*>> runners;
    {
        std::vector<std::string> behaviors;
        for (const auto& b : ctx.Units(UnitKind::Behavior)) if (ctx.InScope(b)) behaviors.push_back(b);
        if (!ctx.Prefetch(behaviors, err)) return false;
        for (const Character& c : ctx.Characters()) {
            std::vector<std::string> graphs;
            if (!ctx.GraphsOf(c, graphs, err)) return false;
            for (const auto& g : graphs) if (ctx.InScope(g)) runners[g].push_back(&c);
        }
        for (const auto& b : behaviors) if (!runners.count(b)) { ctx.Release(b); ++m_unrun; }
    }

    std::map<std::string, RunChar> chars;   ///< character key -> its state, made on first need
    const auto runChar = [&](const Character& c) -> RunChar* {
        if (auto it = chars.find(c.key); it != chars.end()) return it->second.hky ? &it->second : nullptr;
        RunChar& rc = chars[c.key];
        rc.character = &c;
        rc.rig       = ctx.RigBones(c);
        rc.hky       = ctx.Unit(c.key, err);
        if (!rc.hky) return nullptr;
        auto v = characterVocab(*rc.hky);
        if (!v || !rc.rig) { rc.hky = nullptr; return nullptr; }
        rc.vocab = *v;
        return &rc;
    };
    /// @brief Make sure `rc` holds property `name` with `bytes`; false when it holds `name` with other content.
    const auto ensureProperty = [&](RunChar& rc, const std::string& name, const Content& c, const std::vector<std::uint8_t>& bytes,
                                    std::size_t indexWidth, bool& clash) -> bool {
        clash = false;
        if (const auto i = rc.vocab.find(name)) {
            const auto have = propertyBytes(rc, *i, c.cls, indexWidth);
            clash = !have || *have != bytes;
            return true;
        }
        if (!rc.edit) {
            rec->chars.push_back({ rc.character->key, rc.vocab.names().size(), 0, 0, false });
            rc.edit = &rec->chars.back();
            if (auto* vs = rc.vocab.values()) {
                rc.edit->wordsBefore    = vs->FieldRef("wordVariableValues").objs.size();
                rc.edit->variantsBefore = vs->FieldRef("variantVariableValues").objs.size();
            }
        }
        bool created = false;
        if (!addProperty(*rc.hky, rc.vocab, reg, name, c.cls, bytes, indexWidth, created, err)) return false;
        rc.edit->createdValues |= created;
        ctx.Touch(rc.character->key);
        return true;
    };

    std::map<std::string, std::string> byContent;       ///< Content::key -> the property it was hoisted into
    std::map<std::string, std::string> contentOfName;   ///< the reverse: a property name -> the content it holds
    for (const auto& [gkey, run] : runners) {
        decompile::ParsedHky* hky = ctx.Unit(gkey, err);
        if (!hky) return false;
        const std::vector<std::string>* graphRig = ctx.Store().Bones(gkey);
        auto gv = graphVocab(*hky);
        std::vector<Site> sites = sitesOf(*hky);
        if (sites.empty()) continue;
        if (!graphRig || !gv) { m_unresolved += sites.size(); continue; }

        /// @note A runner whose rig or vocabulary is unknown could not take the properties and would lose the inline data:
        ///       the graph keeps it.
        std::vector<RunChar*> rcs;
        bool rigless = false;
        for (const Character* c : run)
            if (RunChar* rc = runChar(*c)) rcs.push_back(rc);
            else if (!err.empty()) return false;
            else rigless = true;
        if (rigless) { ++m_rigless; m_unresolved += sites.size(); continue; }

        /// @note A graph whose rule-applied base version is known (the converter's mod graph) takes the base's names for
        ///       the content the base already moved, so a site the mod left alone derives to no change.
        if (const decompile::ParsedHky* twin = ctx.Twin(gkey)) {
            auto& tw = const_cast<decompile::ParsedHky&>(*twin);
            auto tv = graphVocab(tw);
            const auto same = std::find_if(rcs.begin(), rcs.end(), [&](RunChar* rc) { return *rc->rig == *graphRig; });
            for (const Site& t : sitesOf(tw)) {
                const SchemaObject* b = bindingFor(*t.bindable, t.path);
                if (!tv || same == rcs.end() || !b || b->GetInt("bindingType") != kBindCharProp) continue;
                const std::int64_t vi = b->GetInt("variableIndex");
                if (vi < 0 || static_cast<std::size_t>(vi) >= tv->names().size()) continue;
                const std::string& n = tv->names()[static_cast<std::size_t>(vi)];
                const schema::Field& tf = *t.owner->Fields()[t.field];
                const std::size_t    tw2 = tf.kind == FieldKind::Scalar ? static_cast<std::size_t>(schema::ScalarWidth(tf.scalar)) : 2;
                const auto           i   = (*same)->vocab.find(n);
                const auto           raw = i ? propertyBytes(**same, *i, tf.charProp, tw2) : std::nullopt;
                if (const auto c = raw ? contentFromRaw(tf.charProp, *raw, *graphRig, tw2) : std::nullopt) {
                    byContent.emplace(c->key, n);
                    contentOfName.emplace(n, c->key);
                }
            }
        }

        HoistRecord::GraphEdit ge{ gkey, gv->names().size(), {} };
        for (std::size_t si = 0; si < sites.size(); ++si) {
            const Site& s = sites[si];
            const schema::Field& f = *s.owner->Fields()[s.field];
            auto& value = s.owner->FieldAt(s.field);
            if (decompile::CharPropNeutral(f, value)) continue;
            SchemaObject* bound = bindingFor(*s.bindable, s.path);
            if (bound && (bound->GetInt("bindingType") != kBindCharProp || bound->GetInt("bitIndex") != -1)) { ++m_otherBound; continue; }
            const auto content = contentOf(s, *graphRig);
            if (!content) { ++m_unresolved; continue; }
            const std::size_t width = f.kind == FieldKind::Scalar ? static_cast<std::size_t>(schema::ScalarWidth(f.scalar))
                                                                  : 2;

            /// @brief The character property `n` as `rc` held it before this run, or nullopt.
            const auto vanillaHas = [](RunChar* rc, const std::string& n) -> std::optional<std::size_t> {
                const auto i = rc->vocab.find(n);
                const std::size_t before = rc->edit ? rc->edit->propsBefore : rc->vocab.names().size();
                return i && *i < before ? i : std::nullopt;
            };
            /// @brief Whether `n` can carry this content: no other content was hoisted into it, and every runner holding it
            ///        holds exactly what this site resolves to in its rig.
            const auto free = [&](const std::string& n) {
                if (n.empty()) return false;
                if (const auto t = contentOfName.find(n); t != contentOfName.end() && t->second != content->key) return false;
                for (RunChar* rc : rcs)
                    if (const auto i = rc->vocab.find(n)) {
                        const auto have = propertyBytes(*rc, *i, content->cls, width);
                        if (!have || *have != resolve(*content, *graphRig, *rc->rig, width)) return false;
                    }
                return true;
            };
            const auto mint = [&](const std::string& base) {
                std::string n = base;
                for (int k = 2; !free(n) || std::any_of(rcs.begin(), rcs.end(), [&](RunChar* rc) { return rc->vocab.find(n).has_value(); }) ||
                                gv->find(n).has_value();
                     ++k)
                    n = base + "_" + std::to_string(k);
                ++m_properties;
                return n;
            };
            const std::string& bone = content->names[0];
            /// @note A single bone's property is the same data wherever it is minted (that bone's index), so it carries no
            ///       prefix; a bone set minted in a scoped run carries the run's (a mod bundle's).
            const std::string set  = s.bindable->IndexOf("name") ? s.label + "_" + identifier(s.path) : s.label;
            const std::string base = content->cls == "bone" ? (bone[0] == '#' ? "Bone_Index" + bone.substr(1) : "Bone_" + identifier(bone))
                                   : ctx.NamePrefix().empty() ? set : identifier(ctx.NamePrefix()) + "_" + set;

            /// @brief Each runner's bytes for the property this site ends up bound to.
            std::vector<std::vector<std::uint8_t>> want(rcs.size());
            for (std::size_t r = 0; r < rcs.size(); ++r) want[r] = resolve(*content, *graphRig, *rcs[r]->rig, width);

            std::string        name;
            std::int64_t       reboundFrom = -1;
            if (bound) {
                /// @note Vanilla's binding keeps its property for every runner that holds it; the inline value was the
                ///       fallback of a runner that lacks it, which gets it as that property — unless this run already gave
                ///       that runner the property with another fallback: then the site is re-bound to a property of its own.
                const std::int64_t vi = bound->GetInt("variableIndex");
                if (vi < 0 || static_cast<std::size_t>(vi) >= gv->names().size()) { ++m_unresolved; continue; }
                name = gv->names()[static_cast<std::size_t>(vi)];
                bool conflict = false, typed = true;
                for (std::size_t r = 0; r < rcs.size(); ++r) {
                    if (const auto i = vanillaHas(rcs[r], name)) {
                        const auto have = propertyBytes(*rcs[r], *i, content->cls, width);
                        if (!have) typed = false;
                        else want[r] = *have;
                    } else if (const auto j = rcs[r]->vocab.find(name)) {
                        const auto have = propertyBytes(*rcs[r], *j, content->cls, width);
                        conflict |= !have || *have != want[r];
                    }
                }
                if (!typed) { ++m_unresolved; continue; }
                if (conflict) {
                    reboundFrom = vi;
                    name        = mint(base);
                }
            } else {
                if (const auto it = byContent.find(content->key); it != byContent.end() && free(it->second)) name = it->second;
                /// @note Reuse a property every runner already held, with exactly this content (vanilla's presets). A
                ///       vanilla property some runner lacks is never extended: a binding of it elsewhere relies on that
                ///       runner's fallback.
                for (std::size_t i = 0; name.empty() && !rcs.empty() && i < rcs[0]->vocab.names().size(); ++i) {
                    const std::string& n = rcs[0]->vocab.names()[i];
                    if (!vanillaHas(rcs[0], n) || !free(n)) continue;
                    if (std::all_of(rcs.begin(), rcs.end(), [&](RunChar* rc) { return vanillaHas(rc, n).has_value(); })) name = n;
                }
                if (name.empty()) name = mint(base);
                byContent.emplace(content->key, name);
                contentOfName.emplace(name, content->key);
            }

            for (std::size_t r = 0; r < rcs.size(); ++r) {
                bool clash = false;
                if (!ensureProperty(*rcs[r], name, *content, want[r], width, clash)) return false;
                if (clash && (!bound || reboundFrom >= 0)) { err = gkey + ": property '" + name + "' clashes in " + rcs[r]->character->key; return false; }
            }

            HoistRecord::SiteEdit se;
            se.site        = si;
            se.reboundFrom = reboundFrom;
            if (reboundFrom >= 0) {
                if (!gv->find(name) && !addInfo(*hky, *gv, reg, name, content->cls == "bone" ? kTypeInt32 : kTypePointer, err)) return false;
                bound->SetInt("variableIndex", static_cast<std::int64_t>(*gv->find(name)));
            }
            if (f.kind == FieldKind::Scalar) {
                se.raw = value.raw;
                value.raw.assign(width, 0xff);
            } else {
                auto* set = asSo(value.obj);
                const auto ai = boneArrayField(*set);
                se.raw = set->FieldAt(*ai).raw;
                set->FieldAt(*ai).raw.clear();
            }
            if (!bound) {
                if (!gv->find(name) && !addInfo(*hky, *gv, reg, name, content->cls == "bone" ? kTypeInt32 : kTypePointer, err)) return false;
                auto& sv = s.bindable->FieldRef("variableBindingSet");
                if (!sv.obj) {
                    auto set = make(reg, "hkbVariableBindingSet");
                    if (!set) { err = "no hkbVariableBindingSet class"; return false; }
                    set->SetInt("indexOfBindingToEnable", -1);
                    adopt(*hky, set);
                    sv.obj        = set;
                    se.createdSet = true;
                }
                auto b = make(reg, "hkbVariableBindingSetBinding");
                if (!b) { err = "no hkbVariableBindingSetBinding class"; return false; }
                b->Str("memberPath") = s.path;
                b->SetInt("variableIndex", static_cast<std::int64_t>(*gv->find(name)));
                b->SetInt("bitIndex", -1);
                b->SetInt("bindingType", kBindCharProp);
                adopt(*hky, b);
                asSo(sv.obj)->FieldRef("bindings").objs.push_back(b);
                se.addedBinding = true;
            }
            ge.sites.push_back(std::move(se));
            ++m_hoisted;
        }
        if (!ge.sites.empty()) {
            ctx.Touch(gkey);
            rec->graphs.push_back(std::move(ge));
        }
    }
    if (!rec->graphs.empty() || !rec->chars.empty()) out = std::move(rec);
    return true;
}

bool HoistBoneData::Inverse(Context& ctx, const Record& record, std::string& err) {
    const auto& r = static_cast<const HoistRecord&>(record);
    for (const auto& ge : r.graphs) {
        decompile::ParsedHky* hky = ctx.Unit(ge.key, err);
        if (!hky) return false;
        std::vector<Site> sites = sitesOf(*hky);
        for (auto it = ge.sites.rbegin(); it != ge.sites.rend(); ++it) {
            if (it->site >= sites.size()) { err = ge.key + ": site " + std::to_string(it->site) + " is gone"; return false; }
            const Site& s = sites[it->site];
            const schema::Field& f = *s.owner->Fields()[s.field];
            auto& value = s.owner->FieldAt(s.field);
            if (f.kind == FieldKind::Scalar) value.raw = it->raw;
            else {
                auto* set = asSo(value.obj);
                const auto ai = set ? boneArrayField(*set) : std::nullopt;
                if (!ai) { err = ge.key + ": " + s.path + " holds no bone set"; return false; }
                set->FieldAt(*ai).raw = it->raw;
            }
            if (it->reboundFrom >= 0) {
                SchemaObject* b = bindingFor(*s.bindable, s.path);
                if (!b) { err = ge.key + ": " + s.path + " lost its re-bound binding"; return false; }
                b->SetInt("variableIndex", it->reboundFrom);
            }
            if (!it->addedBinding) continue;
            auto& sv = s.bindable->FieldRef("variableBindingSet");
            auto* set = asSo(sv.obj);
            if (!set) { err = ge.key + ": " + s.path + " lost its binding set"; return false; }
            auto& binds = set->FieldRef("bindings").objs;
            const auto at = std::find_if(binds.rbegin(), binds.rend(), [&](const auto& o) {
                const auto* e = asSo(o);
                return e && e->Str("memberPath") == s.path;
            });
            if (at == binds.rend()) { err = ge.key + ": " + s.path + " has no binding to undo"; return false; }
            binds.erase(std::next(at).base());
            if (it->createdSet && binds.empty()) sv.obj = nullptr;
        }
        auto gv = graphVocab(*hky);
        if (!gv) { err = ge.key + ": no graph data"; return false; }
        if (gv->names().size() < ge.propsBefore) { err = ge.key + ": fewer character properties than before the rule"; return false; }
        gv->names().resize(ge.propsBefore);
        gv->infos().resize(ge.propsBefore);
        ctx.Touch(ge.key);
    }
    for (const auto& ce : r.chars) {
        decompile::ParsedHky* hky = ctx.Unit(ce.key, err);
        if (!hky) return false;
        auto cv = characterVocab(*hky);
        if (!cv || cv->names().size() < ce.propsBefore) { err = ce.key + ": character properties do not match the record"; return false; }
        cv->names().resize(ce.propsBefore);
        cv->infos().resize(ce.propsBefore);
        if (ce.createdValues) cv->owner->FieldRef("characterPropertyValues").obj = nullptr;
        else if (auto* vs = cv->values()) {
            vs->FieldRef("wordVariableValues").objs.resize(ce.wordsBefore);
            vs->FieldRef("variantVariableValues").objs.resize(ce.variantsBefore);
        }
        ctx.Touch(ce.key);
    }
    return true;
}

std::string HoistBoneData::Summary() const {
    return std::to_string(m_hoisted) + " site(s) moved, " + std::to_string(m_properties) + " new property name(s); " +
           std::to_string(m_otherBound) + " bound otherwise, " + std::to_string(m_unresolved) + " kept inline (no rig / index past it), " +
           std::to_string(m_unrun) + " graph(s) no character runs, " + std::to_string(m_rigless) + " graph(s) with a rigless runner";
}

std::vector<BoneProperty> ReadBoneProperties(decompile::ParsedHky& character) {
    std::vector<BoneProperty> out;
    auto v = characterVocab(character);
    if (!v) return out;
    RunChar rc;
    rc.vocab = *v;
    for (std::size_t i = 0; i < v->names().size(); ++i) {
        const long t = v->type(i);
        BoneProperty p{ v->names()[i], {}, {} };
        if (t == kTypeInt32) {
            p.cls = "bone";
            p.bytes = *propertyBytes(rc, i, "bone", 2);
        } else if (t == kTypePointer) {
            auto* o = v->pointer(i);
            if (!o || !boneArrayField(*o)) continue;
            p.cls = o->ClassName();
            p.bytes = *propertyBytes(rc, i, p.cls, 2);
        } else {
            continue;
        }
        out.push_back(std::move(p));
    }
    return out;
}

bool AddBoneProperty(decompile::ParsedHky& character, const schema::SchemaRegistry& reg, const BoneProperty& p, std::string& err) {
    auto v = characterVocab(character);
    if (!v) { err = "not a character unit"; return false; }
    if (const auto i = v->find(p.name)) {
        RunChar rc;
        rc.vocab = *v;
        const auto have = propertyBytes(rc, *i, p.cls, 2);
        if (have && *have == p.bytes) return true;
        err = "character property '" + p.name + "' already holds other data";
        return false;
    }
    bool created = false;
    return addProperty(character, *v, reg, p.name, p.cls, p.bytes, 2, created, err);
}

bool CharPropViolations(Context& ctx, std::vector<std::string>& out, std::string& err) {
    out.clear();
    std::set<std::string> run;
    for (const Character& c : ctx.Characters()) {
        std::vector<std::string> graphs;
        if (!ctx.GraphsOf(c, graphs, err)) return false;
        run.insert(graphs.begin(), graphs.end());
    }
    for (const auto& g : run) {
        const bool cached = ctx.Parsed(g);
        decompile::ParsedHky* hky = ctx.Unit(g, err);
        if (!hky) return false;
        for (const Site& s : sitesOf(*hky)) {
            const schema::Field& f = *s.owner->Fields()[s.field];
            if (decompile::CharPropNeutral(f, s.owner->Values()[s.field])) continue;
            if (const auto* b = bindingFor(*s.bindable, s.path); b && b->GetInt("bindingType") != kBindCharProp) continue;
            out.push_back(g + ": " + s.path + " (" + s.label + ") holds bone data inline");
        }
        if (!cached) ctx.Release(g);
    }
    return true;
}

}  // namespace CB::engine::rules
