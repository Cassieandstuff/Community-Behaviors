#include <codec/format/LoadOrderText.h>
#include <codec/resource/PathKey.h>
#include <codec/serialization/HavokIo.h>
#include <codec/serialization/HkyArchive.h>
#include <decompile/BehaviorDecompile.h>
#include <disk/Disk.h>
#include <interface/HavokEnums.h>
#include <interface/UnitSource.h>
#include <interface/reflection/HavokSchema.h>
#include <merge/LoadPlan.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <type_traits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// @file
/// @brief `hky-tool graph-html`: an arbitrary load order's behavior graphs as one self-contained HTML page, merged the
///        way the game's Resolver merges them (the same load plan, layer order and reflective merge) and rendered
///        REFLECTIVELY: every object's fields come from CB's class schema, so no class is named in this file and a
///        class CB adds shows with no change here. Objects are cards; pointer fields link to their targets and every
///        card lists what references it. Above the graphs, what the load order gets wrong: bundles MO2 shadows, two
///        bundles of one manifest name, units several bundles ship complete, nodes a merge leaves unreachable,
///        behavior references to units no bundle provides, merge failures.

namespace {

    namespace fs   = std::filesystem;
    using Archive  = CB::engine::codec::HkyArchive;
    using Object   = CB::engine::codec::io::SchemaObject;
    using Field    = CB::engine::schema::Field;
    using Kind     = CB::engine::schema::FieldKind;
    using Scalar   = CB::engine::schema::Scalar;
    using CB::engine::codec::resource::PathKey;

    /// @brief One .hky the load order serves: the file that won its name, and the ones MO2 (or a later folder) hid.
    struct Bundle {
        std::string                   stem;       ///< PathKey of the file name's stem: CB's bundle identity
        fs::path                      file;
        std::string                   provider;   ///< the MO2 mod / folder it came from
        std::vector<std::string>      shadowed;   ///< "provider: file" of every same-named bundle it hides
        std::shared_ptr<Archive>      archive;
        std::string                   loadError;
        bool                          hasManifest = false;
        nlohmann::json                manifest;
        std::vector<std::string>      masters;
    };

    struct Layer {
        std::string                                       stem;
        std::shared_ptr<const CB::engine::common::IUnitSource> source;
        bool                                              complete = false;
    };

    std::string Esc(std::string_view s) {
        std::string o;
        o.reserve(s.size());
        for (const char c : s) switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
        return o;
    }

    std::string Num(double v) {
        char b[40];
        std::snprintf(b, sizeof b, "%g", v);
        return b;
    }

    std::string JsonStr(const nlohmann::json& j, const char* key) {
        const auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    std::optional<std::string> ReadText(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }

    /// @brief The load order's bundles: providers lowest priority first, a later provider's file winning its name.
    class BundleSet {
    public:
        void AddProvider(const std::string& provider, const fs::path& pluginsDir) {
            std::error_code ec;
            if (!fs::is_directory(pluginsDir, ec)) return;
            std::vector<fs::path> files;
            for (fs::directory_iterator it(pluginsDir, ec), end; !ec && it != end; it.increment(ec)) {
                const fs::path& p = it->path();
                if (PathKey(p.extension().string()) != ".hky") continue;
                if (it->is_directory(ec)) { m_unpacked.push_back(provider + ": " + p.filename().string()); continue; }
                files.push_back(p);
            }
            std::sort(files.begin(), files.end());
            for (const auto& f : files) AddFile(provider, f);
        }
        void AddFile(const std::string& provider, const fs::path& file) {
            const std::string stem = PathKey(file.stem().string());
            auto [it, fresh] = m_byStem.try_emplace(stem, Bundle{});
            Bundle& b = it->second;
            if (!fresh) b.shadowed.push_back(b.provider + ": " + b.file.filename().string());
            b.stem = stem, b.file = file, b.provider = provider;
        }
        void AddLoadOrder(const fs::path& file) {
            std::error_code ec;
            if (fs::is_regular_file(file, ec)) m_loadOrderFile = file;
        }
        void SetLoadOrder(const fs::path& file) { m_loadOrderFile = file, m_loadOrderPinned = true; }
        bool LoadOrderPinned() const { return m_loadOrderPinned; }
        std::map<std::string, Bundle>& Bundles() { return m_byStem; }
        const std::vector<std::string>& Unpacked() const { return m_unpacked; }
        const std::optional<fs::path>&  LoadOrderFile() const { return m_loadOrderFile; }

    private:
        std::map<std::string, Bundle> m_byStem;   ///< sorted by stem: the scan order the game's directory listing gives
        std::vector<std::string>      m_unpacked;
        std::optional<fs::path>       m_loadOrderFile;
        bool                          m_loadOrderPinned = false;
    };

    /// @brief MO2's view of community_behaviors/: the game's real Data, then every enabled mod from the bottom of
    ///        modlist.txt (lowest priority) to the top, then overwrite/.
    bool AddMo2(BundleSet& set, const fs::path& instance, std::string profile, std::string& err) {
        if (profile.empty()) {
            std::error_code ec;
            for (fs::directory_iterator it(instance / "profiles", ec), end; !ec && it != end; it.increment(ec))
                if (it->is_directory(ec)) { profile = it->path().filename().string(); if (profile == "Default") break; }
        }
        const auto modlist = ReadText(instance / "profiles" / profile / "modlist.txt");
        if (!modlist) { err = "no modlist.txt in profile '" + profile + "' under " + instance.string(); return false; }
        std::vector<std::string> enabled;   // highest priority first, as modlist.txt lists them
        std::istringstream       lines(*modlist);
        for (std::string line; std::getline(lines, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > 1 && line[0] == '+') enabled.push_back(line.substr(1));
        }
        const auto addRoot = [&](const std::string& provider, const fs::path& root) {
            set.AddProvider(provider, root / "community_behaviors" / "plugins");
            if (!set.LoadOrderPinned()) set.AddLoadOrder(root / "community_behaviors" / "loadorder.txt");
        };
        addRoot("game Data", instance / "Game" / "Data");
        for (auto it = enabled.rbegin(); it != enabled.rend(); ++it) addRoot("mod '" + *it + "'", instance / "mods" / *it);
        addRoot("overwrite", instance / "overwrite");
        return true;
    }

    /// @brief The humanoid rig's bone names from Skyrim.hky, so `boneref` fields parse by name (as hky-tool merge-check).
    std::vector<std::string> HumanoidBones(const Archive& skyrim) {
        std::vector<std::string> bones;
        if (const auto list = skyrim.file(PathKey("meshes/actors/character/character assets/skeleton.hkx/bonelist.yaml")))
            for (std::size_t at = 0; at < list->size();) {
                const std::size_t end  = (std::min)(list->find('\n', at), list->size());
                const std::string line = list->substr(at, end - at);
                const auto a = line.find('"'), b = line.rfind('"');
                if (line.find("- ") != std::string::npos && a != std::string::npos && b > a) bones.push_back(line.substr(a + 1, b - a - 1));
                at = end + 1;
            }
        return bones;
    }

    /// @brief A merged graph rendered as cards. Object numbering, back references and orphan detection are all
    ///        derived by walking the schema's pointer fields from the root.
    class GraphPage {
    public:
        GraphPage(const CB::engine::decompile::ParsedHky& g, std::string anchor, std::string baseStem)
            : m_graph(g), m_anchor(std::move(anchor)), m_baseStem(std::move(baseStem)) {
            FindStringData();
            if (g.root) Number(g.root.get());
        }

        std::size_t Reachable() const { return m_order.size(); }
        /// @brief Objects in the merged graph that no pointer from the root reaches: a second complete unit, or a patch
        ///        node whose parent never took it.
        std::vector<const Object*> Orphans() const {
            std::vector<const Object*> out;
            for (const auto& o : m_graph.objects)
                if (o && !m_seen.count(o.get())) out.push_back(o.get());
            return out;
        }
        /// @brief Every object of class `cls` the root reaches, in walk order.
        std::vector<const Object*> OfClass(std::string_view cls) const {
            std::vector<const Object*> out;
            for (const Object* o : m_order)
                if (cls == o->ClassName()) out.push_back(o);
            return out;
        }
        /// @brief The bundle owning `o`: a merge-added node's id is owner-qualified, a base node's (or a sub-object's) is
        ///        the base layer's.
        std::string Owner(const Object* o) const { return OwnerOf(IdOf(o)); }
        const std::string& BaseStem() const { return m_baseStem; }

        std::string Link(const Object* o) const {
            const auto it = m_index.find(o);
            if (it == m_index.end()) return "<span class=bad>(unreachable " + Esc(o->ClassName()) + ")</span>";
            return "<a href=\"#" + m_anchor + "-" + std::to_string(it->second) + "\">#" + std::to_string(it->second) + " " +
                   Esc(o->ClassName()) + Label(o) + "</a>";
        }

        void Render(std::string& h) const {
            for (const Object* o : m_order) Card(h, o, /*orphan*/ false);
            const auto orphans = Orphans();
            if (orphans.empty()) return;
            h += "<h4 class=bad>Unreachable from the root (" + std::to_string(orphans.size()) + ")</h4>";
            for (const Object* o : orphans) Card(h, o, /*orphan*/ true);
        }

    private:
        void FindStringData() {
            for (const auto& o : m_graph.objects)
                if (o && std::string_view(o->ClassName()) == "hkbBehaviorGraphStringData") m_strings = o.get();
                else if (o && std::string_view(o->ClassName()) == "hkbCharacterStringData" && !m_strings) m_strings = o.get();
        }

        /// @brief Card numbers in depth-first order over pointer fields; inline structs belong to their owner's card.
        void Number(const Object* o) {
            if (!o || m_seen.count(o)) return;
            m_seen.insert(o);
            m_index[o] = static_cast<int>(m_order.size());
            m_order.push_back(o);
            Visit(o, o);
        }
        void Visit(const Object* card, const Object* o) {
            const auto& fields = o->Fields();
            const auto& values = o->Values();
            for (std::size_t i = 0; i < fields.size() && i < values.size(); ++i) {
                const Field& f = *fields[i];
                const auto&  v = values[i];
                const auto   edge = [&](const auto& p) {
                    const auto t = CB::engine::codec::io::AsSchema(p);
                    if (!t) return;
                    if (f.kind == Kind::Struct || f.kind == Kind::StructArray) { m_seen.insert(t.get()); Visit(card, t.get()); return; }
                    m_back[t.get()].push_back({ card, f.name });
                    Number(t.get());
                };
                if (f.kind == Kind::Ptr || f.kind == Kind::Struct) edge(v.obj);
                if (f.kind == Kind::PtrArray || f.kind == Kind::StructArray)
                    for (const auto& e : v.objs) edge(e);
            }
        }

        std::string Label(const Object* o) const {
            if (o->HasField("name")) {
                if (const auto* v = o->FindField("name"); v && !v->str.empty()) return " '" + Esc(v->str) + "'";
            }
            return {};
        }

        std::string IdOf(const Object* o) const {
            const auto it = m_graph.identity.ids.find(o);
            return it == m_graph.identity.ids.end() ? std::string() : it->second;
        }
        /// @brief The bundle owning a node: a merge-added node's id is owner-qualified ("stem:local"), a base node's is
        ///        the base layer's.
        std::string OwnerOf(const std::string& id) const {
            const auto c = id.rfind(':');
            return c == std::string::npos ? m_baseStem : id.substr(0, c);
        }

        void Card(std::string& h, const Object* o, bool orphan) const {
            const auto        it  = m_index.find(o);
            const std::string id  = IdOf(o);
            h += "<div class=\"card" + std::string(orphan ? " orphan" : "") + "\"";
            if (it != m_index.end()) h += " id=\"" + m_anchor + "-" + std::to_string(it->second) + "\"";
            h += "><div class=head><b>" + (it != m_index.end() ? "#" + std::to_string(it->second) + " " : std::string()) +
                 Esc(o->ClassName()) + "</b>" + Label(o);
            if (!id.empty()) h += " <span class=id>" + Esc(id) + "</span> <span class=owner>" + Esc(OwnerOf(id)) + "</span>";
            h += "</div>";
            if (const auto b = m_back.find(o); b != m_back.end()) {
                h += "<div class=back>used by ";
                for (std::size_t i = 0; i < b->second.size(); ++i)
                    h += (i ? ", " : "") + Link(b->second[i].first) + " ." + Esc(b->second[i].second);
                h += "</div>";
            }
            Fields(h, o);
            h += "</div>\n";
        }

        void Fields(std::string& h, const Object* o) const {
            h += "<table>";
            const auto& fields = o->Fields();
            const auto& values = o->Values();
            for (std::size_t i = 0; i < fields.size() && i < values.size(); ++i) {
                const Field& f = *fields[i];
                if (f.name.empty() || f.kind == Kind::Vtable || f.kind == Kind::Pad || f.kind == Kind::Skip ||
                    f.kind == Kind::EmptyArray || f.kind == Kind::EmptyPtr)
                    continue;
                h += "<tr><td class=fname title=\"" + Esc(TypeOf(f)) + "\">" + Esc(f.name) + "</td><td>" + Value(o, f, values[i]) + "</td></tr>";
            }
            h += "</table>";
        }

        static std::string TypeOf(const Field& f) {
            if (!f.enumName.empty()) return f.enumName;
            switch (f.kind) {
            case Kind::Ptr: return f.ref + "*";
            case Kind::PtrArray: return f.ref + "*[]";
            case Kind::Struct: return f.ref;
            case Kind::StructArray: return f.ref + "[]";
            case Kind::String:
            case Kind::CString:
            case Kind::Text: return "string";
            case Kind::StringArray: return "string[]";
            case Kind::ScalarArray: return "scalar[]";
            default: return "value";
            }
        }

        std::string Value(const Object* o, const Field& f, const CB::engine::codec::io::FieldValue& v) const {
            switch (f.kind) {
            case Kind::Scalar: return ScalarValue(o, f);
            case Kind::String:
            case Kind::CString:
            case Kind::Text: return "<span class=str>'" + Esc(v.str) + "'</span>";
            case Kind::StringArray: return List(v.strs.size(), [&](std::size_t k) { return "<span class=str>'" + Esc(v.strs[k]) + "'</span>"; });
            case Kind::ScalarArray: return ScalarArray(o, f);
            case Kind::Ptr: {
                const auto t = CB::engine::codec::io::AsSchema(v.obj);
                return t ? Link(t.get()) : "<span class=null>null</span>";
            }
            case Kind::PtrArray:
                return List(v.objs.size(), [&](std::size_t k) {
                    const auto t = CB::engine::codec::io::AsSchema(v.objs[k]);
                    return t ? Link(t.get()) : std::string("<span class=null>null</span>");
                });
            case Kind::Struct: {
                const auto t = CB::engine::codec::io::AsSchema(v.obj);
                if (!t) return "<span class=null>null</span>";
                std::string inner;
                Fields(inner, t.get());
                return inner;
            }
            case Kind::StructArray: {
                std::string inner = "<ol class=structs start=0>";
                for (const auto& e : v.objs)
                    if (const auto t = CB::engine::codec::io::AsSchema(e)) {
                        inner += "<li>";
                        Fields(inner, t.get());
                        inner += "</li>";
                    }
                return inner + "</ol>";
            }
            case Kind::Vector4:
            case Kind::Quaternion:
            case Kind::QsTransform:
            case Kind::Vec4Array:
            case Kind::QsTransformArray: return Floats(v.raw);
            case Kind::BoolArray: {
                std::string bits;
                for (const auto b : v.raw) bits += b ? '1' : '0';
                return bits;
            }
            default: return std::to_string(v.raw.size()) + " bytes";
            }
        }

        template <class F>
        static std::string List(std::size_t n, F&& item) {
            if (n == 0) return "<span class=null>[]</span>";
            std::string out = "<span class=count>" + std::to_string(n) + "</span><ol class=items start=0>";
            for (std::size_t k = 0; k < n; ++k) out += "<li>" + item(k) + "</li>";
            return out + "</ol>";
        }

        static std::string Floats(const std::vector<std::uint8_t>& raw) {
            std::string out;
            for (std::size_t i = 0; i + sizeof(float) <= raw.size(); i += sizeof(float)) {
                float x;
                std::memcpy(&x, raw.data() + i, sizeof x);
                out += (i ? (i % 16 == 0 ? " | " : ", ") : "") + Num(x);
            }
            return out;
        }

        /// @brief An index into the graph's string data, named: `varref`/`eventref` fields, a `charpropif` sibling
        ///        deciding variable vs character property, as CB's .hky emit reads them.
        std::string Named(const Object* o, const Field& f, long long idx) const {
            if (!m_strings || idx < 0) return {};
            std::string list;
            if (!f.eventRef.empty()) list = "eventNames";
            else if (!f.varRef.empty()) {
                list = "variableNames";
                if (const auto eq = f.charPropIf.find('='); eq != std::string::npos) {
                    const std::string sib = f.charPropIf.substr(0, eq), want = f.charPropIf.substr(eq + 1);
                    if (o->HasField(sib) && ScalarText(o, *FieldNamed(o, sib), 0) == want) list = "characterPropertyNames";
                }
            }
            if (list.empty() || !m_strings->HasField(list)) return {};
            const auto* names = m_strings->FindField(list);
            if (!names || idx >= static_cast<long long>(names->strs.size())) return " <span class=bad>(no such entry)</span>";
            return " <span class=str>'" + Esc(names->strs[static_cast<std::size_t>(idx)]) + "'</span>";
        }

        static const Field* FieldNamed(const Object* o, std::string_view name) {
            for (const Field* f : o->Fields())
                if (f->name == name) return f;
            return nullptr;
        }

        static std::string EnumName(const Field& f, long long v) {
            if (f.enumName.empty()) return {};
            const auto& table = CB::engine::common::enums::EnumTable(f.enumName);
            if (!f.isFlags) {
                for (const auto& [name, value] : table)
                    if (value == v) return name;
                return {};
            }
            std::vector<std::string> on;
            for (const auto& [name, value] : table)
                if (value != 0 && (v & value) == value) on.push_back(name);
            std::sort(on.begin(), on.end());
            std::string out;
            for (const auto& n : on) out += (out.empty() ? "" : "|") + n;
            return out;
        }

        static std::string ScalarText(const Object* o, const Field& f, int k) {
            try {
                switch (f.scalar) {
                case Scalar::Float:
                case Scalar::Half: return Num(o->GetFloat(f.name, k));
                case Scalar::Bool: return o->GetBool(f.name, k) ? "true" : "false";
                default: {
                    const long long v = o->GetInt(f.name, k);
                    const std::string e = EnumName(f, v);
                    return e.empty() ? std::to_string(v) : e;
                }
                }
            } catch (const std::exception& e) {
                return std::string("unreadable: ") + e.what();
            }
        }

        std::string ScalarValue(const Object* o, const Field& f) const {
            const int   count = (std::max)(1, f.count);
            std::string out;
            for (int k = 0; k < count; ++k) {
                std::string text = Esc(ScalarText(o, f, k));
                if (!f.varRef.empty() || !f.eventRef.empty()) try {
                        text += Named(o, f, o->GetInt(f.name, k));
                    } catch (const std::exception&) {}
                out += (k ? ", " : "") + text;
            }
            return out;
        }

        template <class T>
        static std::string ArrayText(const Object* o, const Field& f) {
            const auto  values = o->GetArray<T>(f.name);
            std::string out    = "<span class=count>" + std::to_string(values.size()) + "</span> ";
            const std::size_t shown = (std::min)(values.size(), std::size_t{ 256 });
            for (std::size_t i = 0; i < shown; ++i) {
                if constexpr (std::is_floating_point_v<T>) out += (i ? ", " : "") + Num(values[i]);
                else out += (i ? ", " : "") + std::to_string(static_cast<long long>(values[i]));
            }
            if (values.size() > shown) out += " … " + std::to_string(values.size() - shown) + " more";
            return out;
        }

        static std::string ScalarArray(const Object* o, const Field& f) {
            try {
                switch (f.scalar) {
                case Scalar::Int8: return ArrayText<std::int8_t>(o, f);
                case Scalar::Byte:
                case Scalar::Bool: return ArrayText<std::uint8_t>(o, f);
                case Scalar::UInt16: return ArrayText<std::uint16_t>(o, f);
                case Scalar::Int16: return ArrayText<std::int16_t>(o, f);
                case Scalar::UInt32: return ArrayText<std::uint32_t>(o, f);
                case Scalar::Int32: return ArrayText<std::int32_t>(o, f);
                case Scalar::UInt64: return ArrayText<std::uint64_t>(o, f);
                case Scalar::Int64: return ArrayText<std::int64_t>(o, f);
                case Scalar::Float: return ArrayText<float>(o, f);
                case Scalar::Half: break;
                }
            } catch (const std::exception& e) {
                return std::string("unreadable: ") + Esc(e.what());
            }
            return "half[]";
        }

        const CB::engine::decompile::ParsedHky&                                m_graph;
        std::string                                                            m_anchor;
        std::string                                                            m_baseStem;
        const Object*                                                          m_strings = nullptr;
        std::vector<const Object*>                                             m_order;
        std::unordered_map<const Object*, int>                                 m_index;
        std::unordered_set<const Object*>                                      m_seen;
        std::unordered_map<const Object*, std::vector<std::pair<const Object*, std::string>>> m_back;
    };

    /// @brief A unit's character-project root: the folder above its behaviors/ (behavior references are relative to it).
    std::string ProjectRoot(const std::string& unitKey) {
        const auto at = unitKey.rfind("/behaviors/");
        return at == std::string::npos ? std::string() : unitKey.substr(0, at + 1);
    }

    const char* kStyle = R"CSS(
:root{--bg:#fbfbfa;--fg:#1d1d1f;--mute:#6b6b70;--line:#dcdcdf;--card:#ffffff;--bad:#c4301c;--warn:#a96500;--ok:#2d7d46;--link:#1f5fbf;--str:#7a3e9d;--chip:#eef0f4}
@media (prefers-color-scheme:dark){:root{--bg:#141416;--fg:#e8e8ea;--mute:#9a9aa2;--line:#2e2e33;--card:#1c1c20;--bad:#ff7461;--warn:#f0b04a;--ok:#6ccf8a;--link:#7fb0ff;--str:#d3a5f0;--chip:#26262c}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:13px/1.45 ui-sans-serif,system-ui,Segoe UI,sans-serif}
header{position:sticky;top:0;z-index:2;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 16px;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
header h1{font-size:15px;margin:0}header input{flex:1;min-width:200px;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--card);color:var(--fg)}
header button{padding:6px 10px;border:1px solid var(--line);border-radius:6px;background:var(--card);color:var(--fg);cursor:pointer}
main{padding:12px 16px;max-width:1500px}h2{font-size:14px;margin:22px 0 8px}h4{margin:14px 0 6px}
table{border-collapse:collapse}td{padding:2px 8px 2px 0;vertical-align:top}
.sum td,.sum th{border-bottom:1px solid var(--line);padding:4px 10px 4px 0;text-align:left;vertical-align:top}
.diag li{margin:3px 0}.bad{color:var(--bad)}.warn{color:var(--warn)}.ok{color:var(--ok)}
a{color:var(--link);text-decoration:none}a:hover{text-decoration:underline}
details.unit{border:1px solid var(--line);border-radius:8px;margin:8px 0;background:var(--card)}details.unit>summary{padding:8px 12px;cursor:pointer;font-weight:600}
.unitbody{padding:4px 12px 12px}.layers{color:var(--mute);margin:4px 0 10px}
.card{border:1px solid var(--line);border-radius:6px;margin:6px 0;padding:6px 8px;background:var(--bg)}.card:target{outline:2px solid var(--link)}
.card.orphan{border-color:var(--bad)}.head{margin-bottom:3px}.id,.owner{font:11px ui-monospace,Consolas,monospace;padding:1px 5px;border-radius:4px;background:var(--chip);color:var(--mute)}
.back{color:var(--mute);font-size:12px;margin-bottom:3px}.fname{color:var(--mute);font-family:ui-monospace,Consolas,monospace;white-space:nowrap}
.str{color:var(--str)}.null{color:var(--mute)}.count{color:var(--mute);font-size:11px}
ol.items,ol.structs{margin:0;padding-left:28px}ol.structs>li{border-left:2px solid var(--line);padding-left:6px;margin:2px 0}
.hidden{display:none}
)CSS";

    const char* kScript = R"JS(
function openTo(id){const el=document.getElementById(id);if(!el)return;for(let p=el;p;p=p.parentElement)if(p.tagName==='DETAILS')p.open=true;el.scrollIntoView({block:'center'});}
window.addEventListener('hashchange',()=>openTo(location.hash.slice(1)));if(location.hash)openTo(location.hash.slice(1));
document.addEventListener('click',e=>{const a=e.target.closest('a[href^="#"]');if(a)setTimeout(()=>openTo(a.getAttribute('href').slice(1)),0);});
const q=document.getElementById('q');q.addEventListener('input',()=>{const t=q.value.trim().toLowerCase();
document.querySelectorAll('.card').forEach(c=>c.classList.toggle('hidden',t!==''&&!c.textContent.toLowerCase().includes(t)));
if(t)document.querySelectorAll('details.unit').forEach(d=>{if(d.querySelector('.card:not(.hidden)'))d.open=true;});});
document.getElementById('open').onclick=()=>document.querySelectorAll('details.unit').forEach(d=>d.open=true);
document.getElementById('close').onclick=()=>document.querySelectorAll('details.unit').forEach(d=>d.open=false);
)JS";

    void PrintUsage() {
        std::printf("usage: hky-tool graph-html <schemaDir> -o <out.html>\n"
                    "         [--mo2 <instanceDir> [--profile <name>]]   MO2's view: game Data, enabled mods by priority, overwrite\n"
                    "         [--plugins <dir>]...                        a community_behaviors/plugins folder (later wins a name)\n"
                    "         [--bundle <file.hky>]...                    one bundle (later wins a name)\n"
                    "         [--loadorder <loadorder.txt>]               the peer tie-break (default: the load order's own)\n"
                    "         [--unit <unit path>]...                     units to show (default: every unit a non-Skyrim bundle touches)\n"
                    "         [--all]                                     every unit\n");
    }

}  // namespace

/// @brief `graph-html`: see this file's description. Exit 0 when the page was written (diagnostics are content, not
///        failure), 1 when the inputs can't be read, 2 on usage.
int doGraphHtml(int argc, char** argv) {
    std::string              schemaDir, out, mo2, profile;
    std::vector<std::string> pluginDirs, bundleFiles, units;
    std::optional<fs::path>  loadOrder;
    bool                     all = false;
    std::vector<std::pair<std::string, std::string>> sources;   // (kind, path) in argument order
    for (int i = 0; i < argc; ++i) {
        const std::string a    = argv[i];
        const auto        next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "-o") out = next();
        else if (a == "--mo2") sources.push_back({ "mo2", next() });
        else if (a == "--profile") profile = next();
        else if (a == "--plugins") sources.push_back({ "plugins", next() });
        else if (a == "--bundle") sources.push_back({ "bundle", next() });
        else if (a == "--loadorder") loadOrder = next();
        else if (a == "--unit") units.push_back(PathKey(next()));
        else if (a == "--all") all = true;
        else if (schemaDir.empty()) schemaDir = a;
        else { std::printf("unknown argument '%s'\n", a.c_str()); PrintUsage(); return 2; }
    }
    if (schemaDir.empty() || out.empty() || sources.empty()) { PrintUsage(); return 2; }

    CB::engine::schema::SetSharedSchemaDir(schemaDir);
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", CB::engine::schema::SharedRegistryError().c_str()); return 1; }

    BundleSet set;
    if (loadOrder) set.SetLoadOrder(*loadOrder);
    std::string err;
    for (const auto& [kind, path] : sources) {
        if (kind == "mo2" && !AddMo2(set, path, profile, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
        if (kind == "plugins") set.AddProvider("folder " + path, path);
        if (kind == "bundle") set.AddFile("file", path);
    }
    auto& bundles = set.Bundles();
    if (bundles.empty()) { std::printf("FAIL: the load order holds no .hky bundle\n"); return 1; }

    std::unordered_map<std::string, std::vector<std::string>> masters;
    for (auto& [stem, b] : bundles) {
        b.archive = Archive::LoadFromFile(b.file.string(), b.loadError);
        if (!b.archive) continue;
        try {
            if (const auto mf = b.archive->file("manifest.json")) {
                b.manifest    = nlohmann::json::parse(*mf, nullptr, /*allow_exceptions*/ false, /*ignore_comments*/ true);
                b.hasManifest = !b.manifest.is_discarded();
            }
        } catch (const std::exception& e) {
            b.loadError = std::string("manifest.json: ") + e.what();
        }
        if (b.hasManifest)
            if (const auto m = b.manifest.find("masters"); m != b.manifest.end() && m->is_array())
                for (const auto& e : *m)
                    if (e.is_string()) b.masters.push_back(e.get<std::string>());
    }
    /// @note The Resolver plans EVERY bundle: a defaulted manifest (no masters) first, a real one overwriting it.
    for (const auto& [stem, b] : bundles) masters[stem] = b.masters;
    CB::engine::codec::LoadOrder order;
    if (set.LoadOrderFile())
        if (auto lo = CB::engine::codec::ReadLoadOrder(*set.LoadOrderFile())) order = std::move(*lo);
    const auto plan = CB::engine::merge::PlanLoadOrder(masters, order);

    /// @note The Resolver's layer rules: a project or skeleton unit in any bundle takes its key out of the merged graphs.
    std::map<std::string, std::vector<Layer>> layers;
    std::set<std::string>                     nonGraph;
    for (auto& [stem, b] : bundles) {
        if (!b.archive || plan.dropped.count(stem)) continue;
        for (const auto& u : b.archive->units()) {
            if (u.kind == Archive::UnitKind::Project || u.kind == Archive::UnitKind::Skeleton) { nonGraph.insert(u.prefix); continue; }
            if (u.prefix.empty() || u.prefix[0] == '.') continue;
            Layer l{ stem, b.archive->source(u.prefix), false };
            if (const auto g = l.source ? l.source->read("graph.yaml") : std::nullopt) l.complete = !Archive::IsPatchGraphYaml(*g);
            layers[u.prefix].push_back(std::move(l));
        }
    }
    for (const auto& k : nonGraph) layers.erase(k);
    const auto rankOf = [&](const std::string& s) { const auto it = plan.rank.find(s); return it == plan.rank.end() ? 0 : it->second; };
    for (auto& [key, ls] : layers)
        std::stable_sort(ls.begin(), ls.end(), [&](const Layer& a, const Layer& b) { return rankOf(a.stem) < rankOf(b.stem); });

    std::vector<std::string> shown;
    for (const auto& [key, ls] : layers) {
        const bool wanted = all || std::find(units.begin(), units.end(), key) != units.end() ||
                            (units.empty() && std::any_of(ls.begin(), ls.end(), [](const Layer& l) { return l.stem != "skyrim"; }));
        if (wanted) shown.push_back(key);
    }
    for (const auto& u : units)
        if (!layers.count(u)) std::printf("note: no bundle in the load order provides '%s'\n", u.c_str());

    std::vector<std::string> bones;
    if (const auto sk = bundles.find("skyrim"); sk != bundles.end() && sk->second.archive) bones = HumanoidBones(*sk->second.archive);

    std::vector<std::string> problems;     ///< load-order-wide, rendered first
    std::vector<std::string> references;   ///< behavior references that resolve
    {
        std::map<std::string, std::vector<std::string>> byName;
        for (const auto& [stem, b] : bundles)
            if (b.hasManifest && !JsonStr(b.manifest, "name").empty()) byName[JsonStr(b.manifest, "name")].push_back(b.file.filename().string());
        for (const auto& [name, files] : byName)
            if (files.size() > 1) {
                std::string list;
                for (const auto& f : files) list += (list.empty() ? "" : ", ") + Esc(f);
                problems.push_back("<span class=warn>Two bundles call themselves '" + Esc(name) + "'</span>: " + list +
                                   ". CB tells bundles apart by FILE name, so both load; MO2 only replaces a file of the same name.");
            }
        for (const auto& w : plan.warnings) problems.push_back("<span class=warn>Load plan</span>: " + Esc(w));
        for (const auto& u : set.Unpacked()) problems.push_back("<span class=warn>Not read</span>: " + Esc(u) + " is an unpacked bundle folder (this tool reads packed .hky only)");
        for (const auto& [stem, b] : bundles)
            if (!b.archive) problems.push_back("<span class=bad>Unreadable</span> " + Esc(b.file.string()) + ": " + Esc(b.loadError));
        for (const auto& [key, ls] : layers) {
            const auto n = std::count_if(ls.begin(), ls.end(), [](const Layer& l) { return l.complete; });
            if (n > 1) {
                std::string list;
                for (const auto& l : ls) if (l.complete) list += (list.empty() ? "" : ", ") + Esc(l.stem);
                problems.push_back("<span class=bad>Complete in " + std::to_string(n) + " bundles</span>: " + Esc(key) + " (" + list +
                                   "). Only the first is the graph; the others' nodes merge in unreachable.");
            }
        }
    }

    std::string units_html, rows;
    std::size_t index = 0;
    for (const auto& key : shown) {
        const auto& ls     = layers.at(key);
        const std::string anchor = "u" + std::to_string(index++);
        std::vector<CB::engine::common::LayerSource> merge;
        std::string layerText;
        for (const auto& l : ls) {
            const auto mt = plan.masterTable.find(l.stem);
            merge.push_back({ l.source, l.stem, mt == plan.masterTable.end() ? std::vector<std::string>{} : mt->second });
            layerText += (layerText.empty() ? "" : " → ") + Esc(l.stem) + (l.complete ? " <b>(complete)</b>" : " (patch)");
        }
        CB::engine::decompile::ParsedHky graph;
        std::string                      mergeErr;
        bool merged = !bones.empty() && key.starts_with("meshes/actors/character/") &&
                      CB::engine::decompile::MergeLayersReflective(merge, *reg, &bones, graph, mergeErr);
        if (!merged) {
            graph = {};
            std::string plainErr;
            merged = CB::engine::decompile::MergeLayersReflective(merge, *reg, nullptr, graph, plainErr);
            if (!merged) mergeErr = mergeErr.empty() ? plainErr : mergeErr + " / without a rig: " + plainErr;
        }

        std::string status, body;
        if (!merged) {
            status = "<span class=bad>merge failed</span>";
            problems.push_back("<span class=bad>Merge failed</span>: <a href=\"#" + anchor + "\">" + Esc(key) + "</a>: " + Esc(mergeErr));
            body = "<p class=bad>" + Esc(mergeErr) + "</p>";
        } else {
            GraphPage page(graph, anchor, ls.empty() ? std::string() : ls.front().stem);
            /// @note A base node a patch replaced is left unreachable by design; a MOD's node nothing points to is an edit
            ///       that never takes effect, so only those are problems.
            const auto  orphans = page.Orphans();
            std::string modOrphans;
            std::size_t replaced = 0, dangling = 0;
            for (const Object* o : orphans) {
                if (page.Owner(o) == page.BaseStem()) { ++replaced; continue; }
                ++dangling;
                if (dangling <= 8) modOrphans += (modOrphans.empty() ? "" : ", ") + Esc(o->ClassName()) + " (" + Esc(page.Owner(o)) + ")";
            }
            status = std::to_string(page.Reachable()) + " objects";
            if (replaced) status += ", " + std::to_string(replaced) + " replaced";
            if (dangling) {
                status += ", <span class=bad>" + std::to_string(dangling) + " mod object(s) unreachable</span>";
                problems.push_back("<span class=bad>" + std::to_string(dangling) + " mod object(s) nothing points to</span> in <a href=\"#" +
                                   anchor + "\">" + Esc(key) + "</a> (their edits never take effect): " + modOrphans +
                                   (dangling > 8 ? ", …" : ""));
            }
            const std::string root = ProjectRoot(key);
            for (const Object* ref : page.OfClass("hkbBehaviorReferenceGenerator")) {
                if (!ref->HasField("behaviorName")) continue;
                std::string target = ref->FindField("behaviorName")->str;
                std::replace(target.begin(), target.end(), '\\', '/');
                const std::string targetKey = PathKey(root + target);
                const auto        at        = std::find(shown.begin(), shown.end(), targetKey);
                if (!layers.count(targetKey))
                    problems.push_back("<span class=bad>Missing behavior</span>: " + page.Link(ref) + " in <a href=\"#" + anchor + "\">" +
                                       Esc(key) + "</a> references '" + Esc(target) + "' (" + Esc(targetKey) + "), which no bundle provides");
                else if (at != shown.end())
                    references.push_back(Esc(key) + " " + page.Link(ref) + " → <a href=\"#u" +
                                       std::to_string(at - shown.begin()) + "\">" + Esc(targetKey) + "</a>");
            }
            page.Render(body);
        }
        rows += "<tr><td><a href=\"#" + anchor + "\">" + Esc(key) + "</a></td><td>" + layerText + "</td><td>" + status + "</td></tr>";
        units_html += "<details class=unit id=\"" + anchor + "\"><summary>" + Esc(key) + " — " + status + "</summary><div class=unitbody>" +
                      "<div class=layers>layers, base first: " + layerText + "</div>" + body + "</div></details>\n";
        std::printf("  %s: %s\n", key.c_str(), merged ? "merged" : ("FAILED: " + mergeErr).c_str());
    }

    std::string h = "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
                    "<title>CB load order graph</title><style>" + std::string(kStyle) + "</style></head><body>"
                    "<header><h1>CB load order graph</h1><input id=q placeholder=\"filter objects (class, name, field value)…\">"
                    "<button id=open>expand all</button><button id=close>collapse all</button></header><main>";
    h += "<h2>Problems</h2><ul class=diag>";
    if (problems.empty()) h += "<li class=ok>none found</li>";
    for (const auto& p : problems) h += "<li>" + p + "</li>";
    h += "</ul><h2>Behavior references</h2><ul class=diag>";
    if (references.empty()) h += "<li class=null>none among the units shown</li>";
    for (const auto& r : references) h += "<li class=ok>" + r + "</li>";
    h += "</ul><h2>Bundles (" + std::to_string(bundles.size()) + ")</h2><table class=sum><tr><th>file</th><th>from</th><th>manifest</th>"
         "<th>masters</th><th>rank</th><th>hides</th></tr>";
    std::vector<const Bundle*> byRank;
    for (const auto& [stem, b] : bundles) byRank.push_back(&b);
    std::stable_sort(byRank.begin(), byRank.end(), [&](const Bundle* a, const Bundle* b) { return rankOf(a->stem) < rankOf(b->stem); });
    for (const Bundle* b : byRank) {
        std::string ms, hides;
        for (const auto& m : b->masters) ms += (ms.empty() ? "" : ", ") + Esc(m);
        for (const auto& s : b->shadowed) hides += (hides.empty() ? "" : "<br>") + Esc(s);
        const bool dropped = plan.dropped.count(b->stem) != 0;
        h += "<tr><td>" + Esc(b->file.filename().string()) + "</td><td>" + Esc(b->provider) + "</td><td>" +
             (b->hasManifest ? "'" + Esc(JsonStr(b->manifest, "name")) + "' " + Esc(JsonStr(b->manifest, "version")) + " by " +
                                   Esc(JsonStr(b->manifest, "author"))
                             : std::string("<span class=null>none (defaults: no masters)</span>")) +
             "</td><td>" + ms + "</td><td>" + (dropped ? std::string("<span class=bad>dropped</span>") : std::to_string(rankOf(b->stem))) +
             "</td><td>" + (hides.empty() ? std::string() : "<span class=warn>" + hides + "</span>") + "</td></tr>";
    }
    h += "</table><p class=null>loadorder.txt: " +
         (set.LoadOrderFile() ? Esc(set.LoadOrderFile()->string()) : std::string("none (ties break by name)")) + "</p>";
    h += "<h2>Units (" + std::to_string(shown.size()) + " of " + std::to_string(layers.size()) +
         ")</h2><table class=sum><tr><th>unit</th><th>layers, base first</th><th>merged</th></tr>" + rows + "</table>";
    h += "<h2>Graphs</h2>" + units_html + "</main><script>" + kScript + "</script></body></html>";

    if (!CB::engine::disk::WriteFile(out, h, {}, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    std::printf("OK: graph-html -> %s: %zu bundle(s), %zu unit(s) shown of %zu, %zu problem(s).\n", out.c_str(), bundles.size(),
                shown.size(), layers.size(), problems.size());
    return 0;
}
