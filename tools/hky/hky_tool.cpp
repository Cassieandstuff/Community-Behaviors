// hky-tool — the standalone .hky pack/unpack driver over codec/serialization (HkyArchive): the
// base-master diff-strategy tooling, linking only the engine.
//
//   hky-tool pack        <bundleDir> -o <out.hky>        YAML tree  -> single-file .hky (round-trip-checked)
//   hky-tool unpack      <in.hky>    -o <outDir>          .hky       -> YAML tree on disk (original case kept)
//   hky-tool bsa-list    <in.bsa>    [prefix]             list an SSE/AE (v105) archive's files
//   hky-tool bsa-extract <in.bsa>    -o <outDir> [prefix] extract them (archive-stored lowercase paths)
//   hky-tool assemble    <featureDir> <injectables.json> [schemaDir] -o <out.hky>
//                                                         partitions + loose files + generators + inject configs
//   hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>
//                                                         merge + serialize every unit as the game's compile does
//   hky-tool embedded-roundtrip <in.hky> <schemaDir>
//                                                         embedded files parse + splice back byte-exact, curves sample alike
//   hky-tool loadorder   <schemaDir> (--mo2 <instance> [--profile <p>] | --data <Data>) [--unit <key>]... [--all]
//                                                         the .hky load order via VFS + CB-API's document; merge units
//   hky-tool mm-sample   <schemaDir> -o <outDir>          the motion-matching bundle generator over a sample spec
//   hky-tool graph-html  <schemaDir> -o <out.html> --mo2 <instance> | --plugins <dir> | --bundle <f.hky> ...
//                                                         a load order's merged graphs as one reflective HTML page
//
// Case note (Havok is CASE-SENSITIVE): unpack writes each entry at its ORIGINAL-CASE path, never the
// normalized lookup key — a miscased path binds to nothing/wrong at load. Unpack to a SHORT dir (MAX_PATH).

#include <vfs/Bsa.h>
#include <codec/format/AnimationYamlLoader.h>
#include <compile/MotionFeatures.h>
#include <compile/MotionMatchingBundle.h>
#include <codec/resource/PathKey.h>
#include <codec/serialization/HkyArchive.h>
#include <decompile/BehaviorDecompile.h>
#include <decompile/EmbeddedFile.h>
#include <merge/LoadOrderDocument.h>
#include <vfs/Vfs.h>
#include <disk/Disk.h>
#include <interface/MasterTable.h>
#include <interface/reflection/HavokSchema.h>
#include <exec/Parallel.h>
#include <text/Text.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// hky pack: compress an uncompressed bundle YAML tree into a single-file .hky, then re-read it
// (LoadFromFile) to confirm the archive is valid — the pack<->load round-trip sanity.
int doHkyPack(const std::string& dir, const std::string& out) {
    if (out.empty()) { std::printf("usage: hky-tool pack <bundleDir> -o <out.hky>\n"); return 2; }
    std::string err;
    if (!CB::engine::codec::HkyArchive::PackDirectory(dir, out, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    auto arc = CB::engine::codec::HkyArchive::LoadFromFile(out, err);   // round-trip sanity
    if (!arc) { std::printf("FAIL: packed but unreadable: %s\n", err.c_str()); return 1; }
    std::printf("OK: hky pack '%s' -> %s (%zu unit(s)).\n", dir.c_str(), out.c_str(), arc->units().size());
    return 0;
}

// hky unpack: decompress a single-file .hky into its uncompressed YAML tree on disk, ORIGINAL path
// case preserved. For inspecting/diffing a packed master (Skyrim.hky) or any bundle.
int doHkyUnpack(const std::string& hkyPath, const std::string& outDir) {
    if (outDir.empty()) { std::printf("usage: hky-tool unpack <in.hky> -o <outDir>\n"); return 2; }
    std::string err;
    auto arc = CB::engine::codec::HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    // Same ordered range, two views: normalized keys drive file() lookup; original-case paths give the
    // on-disk layout (index-aligned — both iterate the archive's file map in order).
    const auto keys  = arc->filesUnder("");
    const auto paths = arc->filesUnderOrig("");
    namespace fs = std::filesystem;
    std::atomic<std::size_t> written{ 0 };
    try {
        /// @note Each file is read + inflated on the worker that writes it (HkyArchive reads are thread-safe).
        CB::engine::exec::ParallelFor(keys.size(), [&](std::size_t i) {
            const auto content = arc->file(keys[i]);
            if (!content) return;   // keys come from the same map — should always resolve
            const fs::path dst = fs::path(outDir) / fs::path(paths[i]);
            if (std::string werr; !CB::engine::disk::WriteFile(dst, *content, {}, &werr)) throw std::runtime_error(werr);
            ++written;
        });
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    std::printf("OK: hky unpack '%s' -> %s (%zu file(s), %zu unit(s)).\n",
                hkyPath.c_str(), outDir.c_str(), written.load(), arc->units().size());
    return 0;
}

/// @note Path identity (PathKey) on both sides, so "Meshes/Actors" matches the archive's "meshes\actors\…".
bool BsaPrefixMatch(const std::string& path, const std::string& prefix) {
    using CB::engine::codec::resource::PathKey;
    return PathKey(path).rfind(PathKey(prefix), 0) == 0;
}

int doBsaList(const std::string& bsaPath, const std::string& prefix) {
    std::string err;
    const auto arc = vfs::bsa::Archive::Open(bsaPath, &err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    std::size_t shown = 0;
    for (const auto& e : arc->Entries()) {
        if (!prefix.empty() && !BsaPrefixMatch(e.path, prefix)) continue;
        std::printf("%s\t%u%s\n", e.path.c_str(), e.size, e.compressed ? "\tlz4" : "");
        ++shown;
    }
    std::printf("OK: v%u, %zu file(s), %zu listed.\n", arc->Version(), arc->Entries().size(), shown);
    return 0;
}

int doBsaExtract(const std::string& bsaPath, const std::string& outDir, const std::string& prefix) {
    if (outDir.empty()) { std::printf("usage: hky-tool bsa-extract <in.bsa> -o <outDir> [prefix]\n"); return 2; }
    std::string err;
    const auto arc = vfs::bsa::Archive::Open(bsaPath, &err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    namespace fs = std::filesystem;
    std::size_t written = 0, failed = 0;
    std::vector<std::uint8_t> bytes;
    for (const auto& e : arc->Entries()) {
        if (!prefix.empty() && !BsaPrefixMatch(e.path, prefix)) continue;
        if (!arc->Extract(e, bytes, &err)) { std::printf("  FAIL %s: %s\n", e.path.c_str(), err.c_str()); ++failed; continue; }
        std::string rel = e.path;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const fs::path dst = fs::path(outDir) / fs::path(rel);
        if (std::string werr; !CB::engine::disk::WriteFile(dst, bytes, {}, &werr)) { std::printf("  FAIL %s\n", werr.c_str()); ++failed; continue; }
        ++written;
    }
    std::printf("%s: bsa extract '%s' -> %s (%zu written, %zu failed).\n", failed ? "FAIL" : "OK",
                bsaPath.c_str(), outDir.c_str(), written, failed);
    return failed ? 1 : 0;
}

/// @brief The build's config-driven injection: `inject/**/*.json` configs write values into named fields of files in a
///        feature's partitions, as allowed by the injectables table (tools/hky/injectables.json: file kind -> field ->
///        value type).
namespace assemble {

using Json = nlohmann::ordered_json;

/// @brief `text` parsed as JSON, with a key repeated inside one object rejected.
/// @note  nlohmann keeps the last of two equal keys silently, which would drop a tag's first definition unseen.
Json ParseStrict(const std::string& text) {
    std::vector<std::vector<std::string>> open;
    const Json::parser_callback_t cb = [&](int, Json::parse_event_t ev, Json& parsed) {
        if (ev == Json::parse_event_t::object_start) open.emplace_back();
        else if (ev == Json::parse_event_t::object_end && !open.empty()) open.pop_back();
        else if (ev == Json::parse_event_t::key && !open.empty()) {
            const auto key = parsed.get<std::string>();
            if (std::find(open.back().begin(), open.back().end(), key) != open.back().end())
                throw std::runtime_error("duplicate key \"" + key + "\"");
            open.back().push_back(key);
        }
        return true;
    };
    return Json::parse(text, cb);
}

/// @brief The file kinds the injector knows; a kind in the injectables table must be one of these.
/// @note  Recognizing a kind is format knowledge, so it lives here; the table only says what each kind may receive.
enum class Kind { Animation };
/// @brief The value a field takes; a type in the injectables table must be one of these shapes.
/// @note  curveMap = {name: [[time, value] or [time, value, interp], ...]}; oneOf = one of the listed names, as a string;
///        bool = true or false.
struct FieldType {
    enum class Shape { CurveMap, OneOf, Bool } shape = Shape::CurveMap;
    std::vector<std::string>                   oneOf;
};

using Table = std::map<Kind, std::map<std::string, FieldType>>;

/// @brief The fields whose injected value this tool reads back through the animation loader.
/// @note  A field the loader does not read would inject cleanly and then do nothing (the loader skips unknown keys),
///        so the table may name only fields with a read-back (InjectAnimation).
constexpr std::array<std::string_view, 3> kAnimationReadBack{ "curves", "motionTarget", "preload" };

/// @brief A path that can name an animation: a bare `.hkx` FILE, not inside a unit (a unit is a `.hkx/` folder).
bool IsAnimationPath(const std::string& path) {
    using CB::engine::codec::resource::PathKey;
    return CB::engine::codec::resource::HasExt(path, ".hkx") && PathKey(path).find(".hkx/") == std::string::npos;
}

/// @brief An animation: an animation path whose YAML root key is `animation`.
bool IsAnimation(const std::string& path, std::string_view text) {
    if (!IsAnimationPath(path)) return false;
    text = CB::engine::text::StripBom(text);
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end  = (std::min)(text.find('\n', at), text.size());
        std::string_view  line = text.substr(at, end - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (!line.empty() && line.front() != '#') return line == "animation:";
        at = end + 1;
    }
    return false;
}

/// @brief Empty when `v` fits `type`, else why not.
/// @note  A curveMap's keys must start at 0 or later and run in time order; whether they suit the clip (inside its
///        duration) is the consumer's check.
std::string CheckType(const FieldType& type, const Json& v) {
    switch (type.shape) {
    case FieldType::Shape::OneOf: {
        if (v.is_string() && std::find(type.oneOf.begin(), type.oneOf.end(), v.get<std::string>()) != type.oneOf.end()) return {};
        std::string names;
        for (const auto& n : type.oneOf) names += (names.empty() ? "" : ", ") + n;
        return "not one of " + names + ": " + v.dump();
    }
    case FieldType::Shape::Bool:
        return v.is_boolean() ? std::string() : "not true or false: " + v.dump();
    case FieldType::Shape::CurveMap:
        if (!v.is_object()) return "a curveMap is {name: [[time, value] or [time, value, interp], ...]}";
        for (const auto& [name, keys] : v.items()) {
            if (name.empty()) return "an empty name";
            if (!keys.is_array() || keys.empty()) return "'" + name + "' is not a non-empty list of keys";
            double last = 0;
            for (const auto& k : keys) {
                const bool interp = k.is_array() && k.size() == 3 && k[2].is_string() &&
                                    (k[2].get<std::string>() == "CONSTANT" || k[2].get<std::string>() == "LINEAR");
                if (!k.is_array() || (k.size() != 2 && !interp) || !k[0].is_number() || !k[1].is_number())
                    return "'" + name + "' has a key that is not [time, value] or [time, value, \"CONSTANT\"|\"LINEAR\"]: " + k.dump();
                const double t = k[0].get<double>();
                if (t < 0 || (&k != &keys.front() && t < last))
                    return "'" + name + "' has a key before 0 or out of time order: " + k.dump();
                last = t;
            }
        }
        return {};
    }
    return "unknown type";
}

/// @brief A checked curveMap as curves, in the map's order; a key without an interpolation is LINEAR.
std::vector<CB::engine::anim::AnimCurveDef> CurvesOf(const Json& v) {
    using Key = CB::engine::anim::AnimCurveKey;
    std::vector<CB::engine::anim::AnimCurveDef> out;
    for (const auto& [name, keys] : v.items()) {
        auto& c = out.emplace_back();
        c.name  = name;
        for (const auto& k : keys)
            c.keys.push_back({ k[0].get<float>(), k[1].get<float>(),
                               k.size() == 3 && k[2].get<std::string>() == "CONSTANT" ? Key::Interp::Constant : Key::Interp::Linear });
    }
    return out;
}

bool SameCurves(const std::vector<CB::engine::anim::AnimCurveDef>& a, const std::vector<CB::engine::anim::AnimCurveDef>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].keys.size() != b[i].keys.size()) return false;
        for (std::size_t k = 0; k < a[i].keys.size(); ++k) {
            const auto &x = a[i].keys[k], &y = b[i].keys[k];
            if (x.time != y.time || x.value != y.value || x.interp != y.interp) return false;
        }
    }
    return true;
}

/// @brief `v` as YAML flow text (JSON's own escaping; YAML's double-quoted strings read it the same).
std::string Flow(const Json& v) {
    std::string s;
    if (v.is_object()) {
        for (const auto& [k, x] : v.items()) s += (s.empty() ? "" : ", ") + Json(k).dump() + ": " + Flow(x);
        return "{" + s + "}";
    }
    if (v.is_array()) {
        for (const auto& x : v) s += (s.empty() ? "" : ", ") + Flow(x);
        return "[" + s + "]";
    }
    return v.dump();
}

struct Injection {
    std::string field;
    Json        values;
    std::string config;   ///< the config it came from, for messages
    /// @brief A generator's measured curves (field `curves`, `values` unused): a hand-authored curve of the same name
    ///        from an inject config replaces one whole.
    std::optional<std::vector<CB::engine::anim::AnimCurveDef>> measured;
};

/// @brief The file's `curves`: every measured curve in order, each replaced whole by a hand-authored curve of its name,
///        then the hand-only curves in their config's order.
/// @note  One inject config at most authors a file's curves (assemble's one-config-per-field rule), so a hand name
///        is never authored twice.
std::vector<CB::engine::anim::AnimCurveDef> MergedCurves(const std::vector<Injection>& injections) {
    std::vector<CB::engine::anim::AnimCurveDef> out;
    for (const auto& in : injections)
        if (in.field == "curves" && in.measured) out.insert(out.end(), in.measured->begin(), in.measured->end());
    for (const auto& in : injections) {
        if (in.field != "curves" || in.measured) continue;
        for (auto& c : CurvesOf(in.values)) {
            const auto same = std::find_if(out.begin(), out.end(), [&](const auto& m) { return m.name == c.name; });
            if (same != out.end()) *same = std::move(c);
            else out.push_back(std::move(c));
        }
    }
    return out;
}

/// @brief `text` with every injection written in, each as a child of the root `animation:` mapping.
/// @throws std::runtime_error with why, when a field is already present or does not read back as written.
/// @note  Appended as text, so every other byte of the clip is kept. That relies on `animation:` being the file's last
///        top-level key with 2-space children; the read-back through the loader proves it for each file.
std::string InjectAnimation(const std::string& path, std::string text, const std::vector<Injection>& injections) {
    const auto claim = [&](const std::string& field, const std::string& config) {
        const std::string key = "  " + field + ":";
        for (std::size_t at = 0; at < text.size();) {
            const std::size_t end = (std::min)(text.find('\n', at), text.size());
            if (std::string_view(text).substr(at, end - at).starts_with(key))
                throw std::runtime_error("already has '" + field + "' (" + config + ")");
            at = end + 1;
        }
        if (!text.empty() && text.back() != '\n') text += '\n';
    };
    for (const auto& in : injections) {
        if (in.field == "curves") continue;
        claim(in.field, in.config);
        text += "  " + in.field + ": " + Flow(in.values) + "\n";
    }
    /// @note Every curves injection lands as ONE `curves:` block, measured and hand-authored merged by name.
    const auto curvesIn = std::find_if(injections.begin(), injections.end(), [](const auto& in) { return in.field == "curves"; });
    const auto curves   = MergedCurves(injections);
    if (curvesIn != injections.end()) {
        claim("curves", curvesIn->config);
        text += CB::engine::anim::EmitAnimCurvesYaml(curves);
    }
    using MotionTarget = CB::engine::anim::AnimationDef::MotionTarget;
    const auto def = CB::engine::anim::AnimationYamlLoader::LoadFromString(text, path);
    for (const auto& in : injections) {
        bool same = false;
        if (in.field == "curves") {
            same = SameCurves(def.curves, curves);
        } else if (in.field == "motionTarget") {
            same = def.motionTarget == (in.values.get<std::string>() == "motionMatching" ? MotionTarget::MotionMatching
                                                                                          : MotionTarget::AnimationData);
        } else if (in.field == "preload") {
            same = def.preload == in.values.get<bool>();
        }
        if (!same)
            throw std::runtime_error("'" + in.field + "' did not read back as written (is `animation:` the last top-level key?)");
    }
    return text;
}

/// @brief One file's injections applied, after checking its kind and the table.
std::string Apply(const std::string& path, std::string text, const std::vector<Injection>& injections, const Table& table) {
    if (!IsAnimation(path, text))
        throw std::runtime_error("not a file kind the injector knows (animation: a bare .hkx file whose root key is "
                                 "`animation:`)");
    static const std::map<std::string, FieldType> kNone;
    const auto  row    = table.find(Kind::Animation);
    const auto& fields = row == table.end() ? kNone : row->second;
    for (const auto& in : injections) {
        const auto f = fields.find(in.field);
        if (f == fields.end())
            throw std::runtime_error("animation files accept no '" + in.field + "' (injectables.json; " + in.config + ")");
        if (in.measured) continue;
        if (const auto why = CheckType(f->second, in.values); !why.empty())
            throw std::runtime_error("'" + in.field + "': " + why + " (" + in.config + ")");
    }
    return InjectAnimation(path, std::move(text), injections);
}

/// @brief The injectables table: {kind: {field: type}}, every kind and type one the injector knows.
Table LoadTable(const std::string& tablePath) {
    const auto text = CB::engine::disk::ReadFile(tablePath, { .stripBom = true });
    if (!text) throw std::runtime_error("cannot read " + tablePath);
    const Json j = ParseStrict(*text);
    if (!j.is_object()) throw std::runtime_error(tablePath + ": not {kind: {field: type}}");
    Table table;
    for (const auto& [kind, fields] : j.items()) {
        if (kind != "animation") throw std::runtime_error(tablePath + ": unknown file kind '" + kind + "'");
        if (!fields.is_object()) throw std::runtime_error(tablePath + ": '" + kind + "' is not {field: type}");
        auto& row = table[Kind::Animation];
        for (const auto& [field, type] : fields.items()) {
            const bool plain = !field.empty() && std::all_of(field.begin(), field.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
            if (!plain) throw std::runtime_error(tablePath + ": '" + field + "' is not a plain key name");
            if (std::find(kAnimationReadBack.begin(), kAnimationReadBack.end(), field) == kAnimationReadBack.end())
                throw std::runtime_error(tablePath + ": '" + kind + "." + field + "' has no read-back in hky-tool, so a "
                                         "value the loader ignores would pass unseen");
            FieldType ft;
            if (type.is_string() && type.get<std::string>() == "curveMap") {
                ft.shape = FieldType::Shape::CurveMap;
            } else if (type.is_string() && type.get<std::string>() == "bool") {
                ft.shape = FieldType::Shape::Bool;
            } else if (type.is_object() && type.size() == 1 && type.contains("oneOf") && type["oneOf"].is_array() &&
                       !type["oneOf"].empty()) {
                ft.shape = FieldType::Shape::OneOf;
                for (const auto& n : type["oneOf"]) {
                    if (!n.is_string() || n.get<std::string>().empty())
                        throw std::runtime_error(tablePath + ": '" + kind + "." + field + "' oneOf holds a non-name " + n.dump());
                    ft.oneOf.push_back(n.get<std::string>());
                }
            } else {
                throw std::runtime_error(tablePath + ": '" + kind + "." + field + "' has an unknown type " + type.dump() +
                                         " (known: \"curveMap\", \"bool\", {\"oneOf\": [names]})");
            }
            row[field] = std::move(ft);
        }
    }
    return table;
}

}  // namespace assemble

/// @brief The build's config-driven GENERATORS: `generate/**/*.json` configs name a generator kind this tool knows and
///        its parameters, and the generator writes new files into the bundle from a partition's content, so what is
///        derived from the clips (a behavior over them, the rosters naming them) can never drift from them.
namespace generate {

using Json  = assemble::Json;
using Files = CB::engine::compile::MmFiles;
namespace mm = CB::engine::compile;
using CB::engine::text::Quote;

/// @brief One animation a generator works over: its path in the bundle and its roster name.
struct Clip {
    std::string path;     ///< original-case path in the bundle
    std::string roster;   ///< MmAnimationName: the roster entry and the clip's animationName
    std::string stem;     ///< the file name without `.hkx`
};

/// @brief A self-owned FormID (`0000x…`), the id a node a bundle adds carries.
std::string OwnId(unsigned local) {
    char b[16];
    std::snprintf(b, sizeof(b), "0000x%04x", local);
    return b;
}

/// @brief The config's parameters: exactly `keys` (each a non-empty string) and `other` (any value) present, and
///        `optional` (a non-empty string when present) allowed.
void ExpectKeys(const Json& j, std::initializer_list<const char*> keys, std::initializer_list<const char*> other = {},
                std::initializer_list<const char*> optional = {}) {
    if (!j.is_object()) throw std::runtime_error("not an object");
    const auto in = [](std::initializer_list<const char*> l, const std::string& k) {
        return std::any_of(l.begin(), l.end(), [&](const char* x) { return k == x; });
    };
    for (const auto& [k, v] : j.items())
        if (!in(keys, k) && !in(other, k) && !in(optional, k)) throw std::runtime_error("unknown parameter '" + k + "'");
    const auto isName = [&](const char* k) { return j[k].is_string() && !j[k].get<std::string>().empty(); };
    for (const char* k : keys) {
        if (!j.contains(k)) throw std::runtime_error(std::string("missing parameter '") + k + "'");
        if (!isName(k)) throw std::runtime_error(std::string("'") + k + "' is not a non-empty string");
    }
    for (const char* k : other)
        if (!j.contains(k)) throw std::runtime_error(std::string("missing parameter '") + k + "'");
    for (const char* k : optional)
        if (j.contains(k) && !isName(k)) throw std::runtime_error(std::string("'") + k + "' is not a non-empty string");
}

/// @brief The partition's animations under `prefix`, sorted by path: the ORDER every generator over the same clips
///        shares (a selector's child index is this index).
/// @note  `prefix` and `rosterRoot` match by path identity; the roster names keep the bundle's original case.
std::vector<Clip> ClipsUnder(const CB::engine::codec::HkyArchive& arc, const std::string& prefix, const std::string& rosterRoot) {
    using CB::engine::codec::resource::PathKey;
    const std::string want = PathKey(prefix), root = PathKey(rosterRoot);
    if (!want.starts_with(root)) throw std::runtime_error("'clips' is not below 'rosterRoot'");
    std::vector<Clip> clips;
    for (const auto& path : arc.filesUnderOrig("")) {
        std::string roster = mm::MmAnimationName(path, rosterRoot);
        if (roster.empty() || !PathKey(path).starts_with(want)) continue;
        const auto slash = path.rfind('/');
        clips.push_back({ path, std::move(roster), path.substr(slash + 1, path.size() - slash - 1 - 4) });
    }
    std::sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.path < b.path; });
    if (clips.empty()) throw std::runtime_error("no animations under '" + prefix + "'");
    return clips;
}

/// @brief A motion-matching spec of one database `database` holding every clip, in clip order: no chooser rows, the
///        fallback empty (every database).
mm::MmBundleSpec OneDatabase(const std::vector<Clip>& clips, const std::string& rosterRoot, const std::string& database) {
    mm::MmBundleSpec spec;
    spec.rosterRoot = rosterRoot;
    mm::MmDatabase db;
    db.name = database;
    for (const auto& c : clips) db.clips.push_back({ c.path, c.roster, 0 });
    spec.databases.push_back(std::move(db));
    return spec;
}

/// @brief The motion-matching databases a `databases` list names: [{"name", "under", "standingCandidate",
///        "movingCandidate"}], each clip in the FIRST whose `under` prefix holds it (path identity), clips in clip order.
/// @throws std::runtime_error on a malformed entry, a repeated name, a database no clip falls in, or a clip in none.
/// @note  The gates are written as given: the plugin owns the expression language and compiles them.
std::vector<mm::MmDatabase> DatabasesByFolder(const Json& list, const std::vector<Clip>& clips) {
    using CB::engine::codec::resource::PathKey;
    if (!list.is_array() || list.empty()) throw std::runtime_error("'databases' is not a non-empty list");
    std::vector<mm::MmDatabase> dbs;
    std::vector<std::string>    prefixes;
    for (const auto& d : list) {
        ExpectKeys(d, { "name", "under" }, { "standingCandidate", "movingCandidate" });
        if (!d["standingCandidate"].is_string() || !d["movingCandidate"].is_string())
            throw std::runtime_error("database '" + d["name"].get<std::string>() + "': a candidate gate is not a string");
        mm::MmDatabase db;
        db.name              = d["name"].get<std::string>();
        db.standingCandidate = d["standingCandidate"].get<std::string>();
        db.movingCandidate   = d["movingCandidate"].get<std::string>();
        if (std::any_of(dbs.begin(), dbs.end(), [&](const auto& x) { return x.name == db.name; }))
            throw std::runtime_error("database '" + db.name + "' is listed twice");
        dbs.push_back(std::move(db));
        prefixes.push_back(PathKey(d["under"].get<std::string>()));
    }
    for (const auto& c : clips) {
        const auto key = PathKey(c.path);
        std::size_t at = 0;
        while (at < prefixes.size() && !key.starts_with(prefixes[at])) ++at;
        if (at == prefixes.size()) throw std::runtime_error("'" + c.path + "' is under no database's 'under'");
        dbs[at].clips.push_back({ c.path, c.roster, 0 });
    }
    for (const auto& db : dbs)
        if (db.clips.empty()) throw std::runtime_error("database '" + db.name + "' holds no clip");
    return dbs;
}

/// @brief `clipSelectorBehavior`: a complete behavior unit whose root is a selector (`rootClass`) over one clip per
///        animation, the selected child bound to `clipVariable` and every child's user-controlled time to
///        `timeVariable` (only the selected child plays, so one shared time is safe).
/// @note  A CBPoseSearchGenerator root is the engine's motion-matching unit, its databases from `databases`
///        (DatabasesByFolder; children run database by database). A hkbManualSelectorGenerator's indices are int8: it
///        clamps to (int8)(children - 1), so past 128 children every selection reads outside them. Its unit's own ids:
///        0000x0001 the root container, 0000x0002 the selector, 0000x0003+ the clips in clip order.
Files ClipSelectorBehavior(const Json& j, const std::vector<Clip>& clips, const CB::engine::schema::SchemaRegistry* reg) {
    const auto out = j["out"].get<std::string>(), selector = j["selectorName"].get<std::string>();
    const auto clipVar = j["clipVariable"].get<std::string>(), timeVar = j["timeVariable"].get<std::string>();
    const auto rootClass = j["rootClass"].get<std::string>();
    if (rootClass == "CBPoseSearchGenerator") {
        if (!reg) throw std::runtime_error("a CBPoseSearchGenerator unit is built through the schema: pass assemble a <schemaDir>");
        if (!j.contains("databases")) throw std::runtime_error("a CBPoseSearchGenerator unit needs 'databases'");
        mm::MmBundleSpec spec;
        spec.rosterRoot   = j["rosterRoot"].get<std::string>();
        spec.databases    = DatabasesByFolder(j["databases"], clips);
        spec.unitPath     = out;
        spec.selectorName = selector;
        spec.clipVariable = clipVar;
        spec.timeVariable = timeVar;
        Files       files;
        std::string err;
        if (!mm::BuildMotionMatchingBehavior(spec, *reg, files, err)) throw std::runtime_error(err);
        return files;
    }
    if (rootClass != "hkbManualSelectorGenerator")
        throw std::runtime_error("'rootClass' " + rootClass + " is neither hkbManualSelectorGenerator nor CBPoseSearchGenerator");
    if (j.contains("databases")) throw std::runtime_error("'databases' are a CBPoseSearchGenerator's");
    if (clips.size() > 128)
        throw std::runtime_error(std::to_string(clips.size()) + " clips is past what " + rootClass + "'s index addresses (128)");
    const auto slash = out.rfind('/');
    std::string graphName = out.substr(slash == std::string::npos ? 0 : slash + 1);
    if (!CB::engine::codec::resource::HasExt(graphName, ".hkx")) throw std::runtime_error("'out' is not a .hkx unit path");
    graphName = graphName.substr(0, graphName.size() - 4) + ".hkb";

    Files files;
    files.emplace_back(out + "/graph.yaml",
        "id: " + OwnId(1) + "\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbBehaviorGraph':\n"
        "    className: 'hkbBehaviorGraph'\n    variant:\n      class: hkbBehaviorGraph\n      userData: 0\n"
        "      name: " + Quote(graphName) + "\n      variableMode: VARIABLE_MODE_DISCARD_WHEN_INACTIVE\n"
        "      rootGenerator: " + OwnId(2) + "\n      data:\n        variables:\n"
        "          " + Quote(clipVar) + ": {type: VARIABLE_TYPE_INT32, value: 0}\n"
        "          " + Quote(timeVar) + ": {type: VARIABLE_TYPE_REAL, value: 0}\n"
        "        characterProperties: {}\n        events: {}\n        attributes: {}\n");

    const std::string sel = out + "/selectors/" + OwnId(2) + "/";
    files.emplace_back(sel + OwnId(2) + ".yaml",
        "id: " + OwnId(2) + "\nclass: " + rootClass + "\nvariableBindingSet:\n  indexOfBindingToEnable: -1\n"
        "userData: 0\nname: " + Quote(selector) + "\nselectedGeneratorIndex: 0\ncurrentGeneratorIndex: 0\n");
    std::string children;
    for (std::size_t i = 0; i < clips.size(); ++i)
        children += Quote(OwnId(static_cast<unsigned>(i))) + ": " + OwnId(static_cast<unsigned>(i + 3)) + "\n";
    files.emplace_back(sel + "generators.yaml", children);
    files.emplace_back(sel + "variableBindingSet.bindings.yaml",
        "'selectedGeneratorIndex':\n  variableIndex: 0\n  variable: " + Quote(clipVar) +
        "\n  bitIndex: -1\n  bindingType: BINDING_TYPE_VARIABLE\n");

    for (std::size_t i = 0; i < clips.size(); ++i) {
        const std::string id = OwnId(static_cast<unsigned>(i + 3)), dir = out + "/clips/" + id + "/";
        files.emplace_back(dir + id + ".yaml",
            "id: " + id + "\nclass: hkbClipGenerator\nvariableBindingSet:\n  indexOfBindingToEnable: -1\nuserData: 0\n"
            "name: " + Quote(clips[i].stem) + "\nanimationName: " + Quote(clips[i].roster) + "\n"
            "cropStartAmountLocalTime: 0\ncropEndAmountLocalTime: 0\nstartTime: 0\nplaybackSpeed: 1\nenforcedDuration: 0\n"
            "userControlledTimeFraction: 0\nanimationBindingIndex: -1\nmode: MODE_USER_CONTROLLED\nflags: 0\n");
        files.emplace_back(dir + "variableBindingSet.bindings.yaml",
            "'userControlledTimeFraction':\n  variableIndex: 1\n  variable: " + Quote(timeVar) +
            "\n  bitIndex: -1\n  bindingType: BINDING_TYPE_VARIABLE\n");
    }
    return files;
}

/// @brief `rosterPatch`: one patch unit per listed character adding every animation to its roster (the engine's
///        motion-matching roster patch over one database of all the clips).
Files RosterPatch(const Json& j, const std::vector<Clip>& clips) {
    const Json& chars = j["characters"];
    if (!chars.is_array() || chars.empty()) throw std::runtime_error("'characters' is not a non-empty list");
    auto spec = OneDatabase(clips, j["rosterRoot"].get<std::string>(), "roster");
    for (const auto& c : chars) {
        ExpectKeys(c, { "path", "root" });
        spec.characters.emplace_back(c["path"].get<std::string>(), c["root"].get<std::string>());
    }
    Files       files;
    std::string err;
    if (!mm::BuildMotionMatchingRosters(spec, files, err)) throw std::runtime_error(err);
    return files;
}

/// @brief `motionCurves`: every animation under `clips` in the partition, measured and given its motion curves
///        (MeasureMotionCurves) simplified within `tolerance` ({"Speed", "MoveDirection", "TurnRate", "Acceleration"}).
/// @return each animation's path (original case) and its curves, for injection into that same file.
/// @note  Not a file generator: the curves belong in the partition's own clips, so they are injected there, where an
///        inject config's hand-authored curve of the same name replaces a measured one whole.
std::vector<std::pair<std::string, std::vector<CB::engine::anim::AnimCurveDef>>>
MotionCurves(const Json& j, const std::map<std::string, std::shared_ptr<CB::engine::codec::HkyArchive>>& partitions) {
    namespace ea = CB::engine::anim;
    using CB::engine::codec::resource::PathKey;
    Json params = j;
    params.erase("generator");
    ExpectKeys(params, { "from", "clips" }, { "tolerance" });
    const Json& t = params["tolerance"];
    static constexpr std::array<const char*, 4> kNames{ "Speed", "MoveDirection", "TurnRate", "Acceleration" };
    if (!t.is_object() || t.size() != kNames.size())
        throw std::runtime_error("'tolerance' is not {\"Speed\", \"MoveDirection\", \"TurnRate\", \"Acceleration\"}");
    std::array<double, 4> tv{};
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (!t.contains(kNames[i]) || !t[kNames[i]].is_number() || t[kNames[i]].get<double>() < 0)
            throw std::runtime_error(std::string("'tolerance' has no non-negative ") + kNames[i]);
        tv[i] = t[kNames[i]].get<double>();
    }
    const ea::CurveTolerances tol{ tv[0], tv[1], tv[2], tv[3] };

    const auto from = params["from"].get<std::string>();
    const auto p    = partitions.find(from);
    if (p == partitions.end()) throw std::runtime_error("no partition '" + from + "' in the feature");
    const std::string prefix = PathKey(params["clips"].get<std::string>());
    std::vector<std::string> paths;
    for (const auto& path : p->second->filesUnderOrig(""))
        if (assemble::IsAnimationPath(path) && PathKey(path).starts_with(prefix)) paths.push_back(path);
    if (paths.empty()) throw std::runtime_error("no animations under '" + params["clips"].get<std::string>() + "'");

    std::vector<std::pair<std::string, std::vector<ea::AnimCurveDef>>> out(paths.size());
    std::vector<std::string>                                           errs(paths.size());
    CB::engine::exec::ParallelFor(paths.size(), [&](std::size_t i) {
        try {
            const auto text = p->second->file(PathKey(paths[i]));
            if (!text) throw std::runtime_error("unreadable");
            std::string err;
            const auto  m = ea::MeasureClip(ea::AnimationYamlLoader::LoadFromString(*text, paths[i]), err);
            if (!m) throw std::runtime_error(err);
            out[i] = { paths[i], ea::MeasureMotionCurves(*m, tol) };
        } catch (const std::exception& e) {
            errs[i] = paths[i] + ": " + e.what();
        }
    });
    std::string failed;
    for (const auto& e : errs)
        if (!e.empty()) failed += (failed.empty() ? "" : "; ") + e;
    if (!failed.empty()) throw std::runtime_error(failed);
    return out;
}

/// @brief One config run: its files, from the partition it names. `reg` is null when assemble was given no schema.
/// @throws std::runtime_error with why, on an unknown generator or a bad parameter.
Files Run(const Json& j, const std::map<std::string, std::shared_ptr<CB::engine::codec::HkyArchive>>& partitions,
          const CB::engine::schema::SchemaRegistry* reg) {
    if (!j.is_object() || !j.contains("generator") || !j["generator"].is_string())
        throw std::runtime_error("not {\"generator\": <kind>, ...}");
    const auto kind = j["generator"].get<std::string>();
    Json params = j;
    params.erase("generator");
    if (kind == "clipSelectorBehavior") {
        Json named = params;
        named.erase("databases");
        ExpectKeys(named, { "from", "clips", "rosterRoot", "out", "rootClass", "selectorName", "clipVariable", "timeVariable" });
    } else if (kind == "rosterPatch") {
        ExpectKeys(params, { "from", "clips", "rosterRoot" }, { "characters" });
    } else {
        throw std::runtime_error("unknown generator '" + kind + "' (known: clipSelectorBehavior, rosterPatch, motionCurves)");
    }
    const auto from = params["from"].get<std::string>();
    const auto p    = partitions.find(from);
    if (p == partitions.end()) throw std::runtime_error("no partition '" + from + "' in the feature");
    const auto clips = ClipsUnder(*p->second, params["clips"].get<std::string>(), params["rosterRoot"].get<std::string>());
    return kind == "clipSelectorBehavior" ? ClipSelectorBehavior(params, clips, reg) : RosterPatch(params, clips);
}

}  // namespace generate

/// @brief `assemble`: a feature folder in the repo -> its shipped .hky. Every `*.hky` at the folder's top is a
///        partition, `inject/` holds the configs, and everything else (manifest.json, loose trees) ships at its path.
/// @note  Every failure is a hard error, never a skip: a config naming a missing path, two configs for one file's
///        field, a field the file's kind does not accept, a value of the wrong shape, two sources supplying one path.
/// @param schemaDir the class schema the generators build through; empty = none (a generator that needs it fails).
int doAssemble(const std::string& dir, const std::string& tablePath, const std::string& schemaDir, const std::string& out) {
    if (dir.empty() || tablePath.empty() || out.empty()) {
        std::printf("usage: hky-tool assemble <featureDir> <injectables.json> [schemaDir] -o <out.hky>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    using CB::engine::codec::HkyArchive;
    using CB::engine::codec::resource::HasExt;
    using CB::engine::codec::resource::PathKey;

    std::vector<std::string> errors;
    assemble::Table          table;
    try {
        table = assemble::LoadTable(tablePath);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    const CB::engine::schema::SchemaRegistry* reg = nullptr;
    if (!schemaDir.empty()) {
        CB::engine::schema::SetSharedSchemaDir(schemaDir);
        reg = CB::engine::schema::SharedRegistry();
        if (!reg) { std::printf("FAIL: schema: %s\n", CB::engine::schema::SharedRegistryError().c_str()); return 1; }
    }

    struct Source {
        HkyArchive::PackEntry           entry;
        std::string                     from;   ///< a partition's name or "loose", for messages
        std::vector<assemble::Injection> injections;
    };
    std::vector<Source>                          sources;
    std::unordered_map<std::string, std::size_t> byKey;
    const auto add = [&](std::string name, std::function<std::optional<std::string>()> read, const std::string& from) {
        const auto [it, fresh] = byKey.emplace(PathKey(name), sources.size());
        if (!fresh) {
            errors.push_back("'" + name + "' is supplied by both " + sources[it->second].from + " and " + from);
            return;
        }
        sources.push_back({ { std::move(name), std::move(read) }, from, {} });
    };

    const fs::path root = fs::absolute(fs::path(dir));
    fs::path       injectRoot, generateRoot;
    std::map<std::string, std::shared_ptr<HkyArchive>> partitions;
    std::size_t    loose = 0;
    std::error_code ec;
    std::vector<fs::directory_entry> top;
    for (fs::directory_iterator it(CB::engine::disk::LongPath(root), ec), end; !ec && it != end; it.increment(ec)) top.push_back(*it);
    if (ec) { std::printf("FAIL: cannot list '%s': %s\n", dir.c_str(), ec.message().c_str()); return 1; }
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
    for (const auto& e : top) {
        const fs::path    child = root / e.path().filename();
        const std::string fname = e.path().filename().generic_string();
        if (e.is_directory() && fname == "inject") { injectRoot = child; continue; }
        if (e.is_directory() && fname == "generate") { generateRoot = child; continue; }
        if (e.is_regular_file() && HasExt(fname, ".hky")) {
            std::string err;
            auto arc = HkyArchive::LoadFromFile(child.string(), err);
            if (!arc) { std::printf("FAIL: partition %s: %s\n", fname.c_str(), err.c_str()); return 1; }
            const auto keys = arc->filesUnder(""), orig = arc->filesUnderOrig("");
            for (std::size_t i = 0; i < keys.size(); ++i)
                add(orig[i], [arc, k = keys[i]] { return arc->file(k); }, fname);
            partitions.emplace(fname, std::move(arc));
            continue;
        }
        const auto addLoose = [&](const fs::path& p) {
            add(p.lexically_relative(root).generic_string(), [p] { return CB::engine::disk::ReadFile(p); }, "loose files");
            ++loose;
        };
        if (e.is_regular_file()) { addLoose(child); continue; }
        if (e.is_directory())
            for (fs::recursive_directory_iterator it(CB::engine::disk::LongPath(child), ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file()) addLoose(child / it->path().lexically_relative(CB::engine::disk::LongPath(child)));
        if (ec) { std::printf("FAIL: cannot walk '%s': %s\n", child.string().c_str(), ec.message().c_str()); return 1; }
    }

    const auto jsonUnder = [&](const fs::path& folder, std::vector<fs::path>& found) {
        if (folder.empty()) return true;
        for (fs::recursive_directory_iterator it(CB::engine::disk::LongPath(folder), ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file() && HasExt(it->path().generic_string(), ".json"))
                found.push_back(folder / it->path().lexically_relative(CB::engine::disk::LongPath(folder)));
        if (ec) { std::printf("FAIL: cannot walk '%s': %s\n", folder.string().c_str(), ec.message().c_str()); return false; }
        std::sort(found.begin(), found.end());
        return true;
    };
    std::vector<fs::path> configs, generators;
    if (!jsonUnder(injectRoot, configs) || !jsonUnder(generateRoot, generators)) return 1;

    /// @note Generators run before injection, so an inject config may target a generated file like any other.
    std::size_t generated = 0, measured = 0;
    for (const auto& g : generators) {
        const std::string name = g.lexically_relative(generateRoot).generic_string();
        try {
            const auto text = CB::engine::disk::ReadFile(g, { .stripBom = true });
            if (!text) throw std::runtime_error("unreadable");
            const auto j = assemble::ParseStrict(*text);
            if (j.is_object() && j.contains("generator") && j["generator"] == "motionCurves") {
                for (auto& [path, curves] : generate::MotionCurves(j, partitions)) {
                    const auto it = byKey.find(PathKey(path));
                    if (it == byKey.end()) throw std::runtime_error("'" + path + "' is not in the feature");
                    sources[it->second].injections.push_back({ "curves", {}, "generate/" + name, std::move(curves) });
                    ++measured;
                }
                continue;
            }
            for (auto& [path, content] : generate::Run(j, partitions, reg)) {
                add(path, [c = std::make_shared<const std::string>(std::move(content))] { return std::optional<std::string>(*c); },
                    "generator " + name);
                ++generated;
            }
        } catch (const std::exception& e) {
            errors.push_back("generate/" + name + ": " + e.what());
        }
    }

    std::map<std::pair<std::size_t, std::string>, std::string> slots;
    std::size_t injected = 0;
    for (const auto& c : configs) {
        const std::string name = c.lexically_relative(injectRoot).generic_string();
        try {
            const auto text = CB::engine::disk::ReadFile(c, { .stripBom = true });
            if (!text) throw std::runtime_error("unreadable");
            const auto j = assemble::ParseStrict(*text);
            if (!j.is_object() || j.size() != 1 || !j.contains("targets") || !j["targets"].is_array())
                throw std::runtime_error("not {\"targets\": [...]}");
            for (const auto& t : j["targets"]) {
                /// @note `path` names one file; `under` names every animation below a path prefix (path identity), so
                ///       one target covers a whole clip set and a clip added under it gets the value too.
                const bool one = t.is_object() && t.contains("path"), many = t.is_object() && t.contains("under");
                const char* where = one ? "path" : "under";
                if (!t.is_object() || t.size() != 3 || one == many || !t.contains("field") || !t.contains("values") ||
                    !t[where].is_string() || !t["field"].is_string())
                    throw std::runtime_error("a target is not {\"path\" or \"under\", \"field\", \"values\"}");
                const auto at = t[where].get<std::string>(), field = t["field"].get<std::string>();
                std::vector<std::size_t> hits;
                if (one) {
                    const auto it = byKey.find(PathKey(at));
                    if (it == byKey.end()) throw std::runtime_error("no file '" + at + "' in the feature");
                    hits.push_back(it->second);
                } else {
                    const std::string prefix = PathKey(at);
                    for (std::size_t s = 0; s < sources.size(); ++s)
                        if (assemble::IsAnimationPath(sources[s].entry.name) && PathKey(sources[s].entry.name).starts_with(prefix))
                            hits.push_back(s);
                    if (hits.empty()) throw std::runtime_error("no animation under '" + at + "' in the feature");
                }
                for (const std::size_t s : hits) {
                    const auto [slot, fresh] = slots.emplace(std::pair{ s, field }, name);
                    if (!fresh)
                        throw std::runtime_error("'" + field + "' of '" + sources[s].entry.name + "' is also set by " + slot->second);
                    sources[s].injections.push_back({ field, t["values"], name });
                    ++injected;
                }
            }
        } catch (const std::exception& e) {
            errors.push_back(name + ": " + e.what());
        }
    }
    if (!errors.empty()) {
        for (const auto& e : errors) std::printf("  FAIL %s\n", e.c_str());
        std::printf("FAIL: assemble '%s': %zu error(s), nothing packed.\n", dir.c_str(), errors.size());
        return 1;
    }

    std::mutex               failMutex;
    std::vector<std::string> failures;
    std::vector<HkyArchive::PackEntry> entries;
    std::size_t targetsTouched = 0;
    for (auto& s : sources) {
        if (!s.injections.empty()) {
            ++targetsTouched;
            s.entry.read = [base = std::move(s.entry.read), name = s.entry.name, inj = std::move(s.injections), &table,
                            &failMutex, &failures]() -> std::optional<std::string> {
                auto text = base();
                if (!text) return std::nullopt;
                try {
                    return assemble::Apply(name, std::move(*text), inj, table);
                } catch (const std::exception& e) {
                    const std::lock_guard lock(failMutex);
                    failures.push_back(name + ": " + e.what());
                    return std::nullopt;
                }
            };
        }
        entries.push_back(std::move(s.entry));
    }

    const std::size_t total = entries.size();
    std::string       err;
    if (!HkyArchive::PackEntries(std::move(entries), out, err)) {
        /// @note An injection failure surfaces from the pack as an unreadable source; its own message says why.
        for (const auto& f : failures) std::printf("  FAIL %s\n", f.c_str());
        if (failures.empty()) std::printf("FAIL: %s\n", err.c_str());
        else std::printf("FAIL: assemble '%s': %zu error(s), nothing packed.\n", dir.c_str(), failures.size());
        return 1;
    }
    if (!HkyArchive::LoadFromFile(out, err)) { std::printf("FAIL: assembled but unreadable: %s\n", err.c_str()); return 1; }
    std::printf("OK: assemble '%s' -> %s: %zu file(s) from %zu partition(s) + %zu loose + %zu generated by %zu "
                "config(s); %zu clip(s) given measured curves; %zu injection(s) from %zu config(s) into %zu file(s).\n",
                dir.c_str(), out.c_str(), total, partitions.size(), loose, generated, generators.size(), measured, injected,
                configs.size(), targetsTouched);
    return 0;
}

/// @brief `embedded-roundtrip`: every file of a `kind: embedded` descriptor in a .hky, parsed through the schema and
///        spliced back unchanged, must reproduce its text byte for byte; and every `view: curve` object must sample
///        (SampleCurve, by its view roles) exactly as the typed loader's curves do, at each key and midway between.
int doEmbeddedRoundTrip(const std::string& hkyPath, const std::string& schemaDir) {
    if (hkyPath.empty() || schemaDir.empty()) {
        std::printf("usage: hky-tool embedded-roundtrip <in.hky> <schemaDir>\n");
        return 2;
    }
    namespace dc = CB::engine::decompile;
    namespace ea = CB::engine::anim;
    CB::engine::schema::SetSharedSchemaDir(schemaDir);
    const auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", CB::engine::schema::SharedRegistryError().c_str()); return 1; }
    std::string err;
    const auto  arc = CB::engine::codec::HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    const auto keys = arc->filesUnder(""), orig = arc->filesUnderOrig("");
    std::atomic<std::size_t> files{ 0 }, curves{ 0 }, samples{ 0 };
    std::mutex               failMutex;
    std::vector<std::string> failures;
    const auto fail = [&](const std::string& f) { const std::lock_guard lock(failMutex); failures.push_back(f); };
    CB::engine::exec::ParallelFor(keys.size(), [&](std::size_t i) {
        try {
            const auto text = arc->file(keys[i]);
            if (!text) return fail(orig[i] + ": unreadable");
            const auto* kind = dc::EmbeddedKindOf(*reg, *text);
            if (!kind) return;
            ++files;
            std::string perr;
            const auto  obj = dc::ParseEmbeddedFile(*text, *kind, *reg, orig[i], perr);
            if (!obj) return fail(perr);
            const auto back = dc::SpliceEmbeddedFile(*text, *kind, *obj, *reg, perr);
            if (!back) return fail(orig[i] + ": " + perr);
            if (*back != *text) {
                std::size_t at = 0;
                while (at < back->size() && at < text->size() && (*back)[at] == (*text)[at]) ++at;
                return fail(orig[i] + ": splice differs at byte " + std::to_string(at));
            }
            if (kind->cls != "CBAnimationCurves") return;
            const auto def = ea::AnimationYamlLoader::LoadFromString(*text, orig[i]);
            const auto& objs = obj->FieldRef("curves").objs;
            if (objs.size() != def.curves.size()) return fail(orig[i] + ": curve count differs");
            for (std::size_t c = 0; c < objs.size(); ++c) {
                const auto* so = dynamic_cast<const CB::engine::codec::io::SchemaObject*>(objs[c].get());
                const auto& typed = def.curves[c];
                ++curves;
                for (std::size_t k = 0; k < typed.keys.size(); ++k) {
                    const float t0 = typed.keys[k].time;
                    const float t1 = k + 1 < typed.keys.size() ? (t0 + typed.keys[k + 1].time) / 2 : t0 + 1;
                    for (const float t : { t0, t1 }) {
                        const auto s = so ? dc::SampleCurve(*so, t, *reg) : std::nullopt;
                        ++samples;
                        if (!s || *s != ea::SampleCurve(typed, t))
                            return fail(orig[i] + ": curve '" + typed.name + "' samples differently at " + std::to_string(t));
                    }
                }
            }
        } catch (const std::exception& e) {
            fail(orig[i] + ": " + e.what());
        }
    });
    for (const auto& f : failures) std::printf("  FAIL %s\n", f.c_str());
    std::printf("%s: embedded-roundtrip '%s': %zu embedded file(s), %zu curve(s), %zu sample(s) checked, %zu failure(s).\n",
                failures.empty() ? "OK" : "FAIL", hkyPath.c_str(), files.load(), curves.load(), samples.load(), failures.size());
    return failures.empty() && files > 0 ? 0 : 1;
}

/// @brief `loadorder`: the .hky load order of a Data folder or MO2 instance, as CB-API's LoadOrderDocument reads it
///        through VFS: its bundles in order, then every behavior and character unit merged (`--all`) or the named ones.
int doLoadOrder(int argc, char** argv) {
    std::string              schemaDir, mo2, profile, data;
    std::vector<std::string> units;
    bool                     all = false;
    for (int i = 0; i < argc; ++i) {
        const std::string a    = argv[i];
        const auto        next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--mo2") mo2 = next();
        else if (a == "--profile") profile = next();
        else if (a == "--data") data = next();
        else if (a == "--unit") units.push_back(next());
        else if (a == "--all") all = true;
        else if (schemaDir.empty()) schemaDir = a;
    }
    if (schemaDir.empty() || (mo2.empty() == data.empty())) {
        std::printf("usage: hky-tool loadorder <schemaDir> (--mo2 <instance> [--profile <p>] | --data <DataFolder>) [--unit <key>]... [--all]\n");
        return 2;
    }
    CB::engine::schema::SetSharedSchemaDir(schemaDir);
    const auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", CB::engine::schema::SharedRegistryError().c_str()); return 1; }

    vfs::Options o;
    if (!mo2.empty()) o.kind = vfs::Options::Kind::Mo2, o.mo2Instance = mo2, o.mo2Profile = profile;
    else o.dataFolder = data;
    vfs::FileSystem fsys;
    std::string     err;
    if (!fsys.Open(o, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    CB::engine::merge::LoadOrderDocument doc;
    if (!doc.Open(fsys, *reg, &err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    for (const auto& b : doc.Bundles())
        std::printf("  %3d %-28s %-34s%s%s\n", b.rank, b.stem.c_str(), b.provider.c_str(), b.dropped ? " DROPPED" : "",
                    b.error.empty() ? "" : (" UNREADABLE: " + b.error).c_str());
    for (const auto& w : doc.Warnings()) std::printf("  warn: %s\n", w.c_str());
    const auto every = doc.Units();
    std::size_t graphs = 0, rigged = 0, projects = 0, skeletons = 0;
    for (const auto& u : every) {
        projects  += u.kind == CB::engine::codec::HkyArchive::UnitKind::Project;
        skeletons += u.kind == CB::engine::codec::HkyArchive::UnitKind::Skeleton;
        if (std::find(units.begin(), units.end(), u.key) == units.end()) continue;
        std::string layers;
        for (const auto& l : u.layers) layers += (layers.empty() ? "" : " -> ") + l;
        std::printf("  %s: kind %d, layers %s\n", u.key.c_str(), static_cast<int>(u.kind), layers.c_str());
        if (u.kind == CB::engine::codec::HkyArchive::UnitKind::Project)
            for (const auto& c : doc.ProjectCharacters(u.key)) std::printf("    character %s\n", c.c_str());
    }
    for (const auto& u : every) {
        using K = CB::engine::codec::HkyArchive::UnitKind;
        if (u.kind != K::Behavior && u.kind != K::Character) continue;
        ++graphs;
        if (doc.RigBones(u.key)) ++rigged;
        if (std::find(units.begin(), units.end(), u.key) != units.end()) {
            std::string rigs;
            for (const auto& r : doc.Rigs(u.key)) rigs += (rigs.empty() ? "" : ", ") + r;
            std::printf("  %s binds: %s\n", u.key.c_str(), rigs.empty() ? "no rig" : rigs.c_str());
        }
        if (all) units.push_back(u.key);
    }
    std::printf("  %zu bundle(s), %zu file(s), %zu unit(s): %zu project(s), %zu skeleton(s), %zu behavior/character "
                "unit(s), %zu bound to a rig\n",
                doc.Bundles().size(), doc.Files().size(), every.size(), projects, skeletons, graphs, rigged);

    std::atomic<std::size_t> merged{ 0 };
    std::mutex               failMutex;
    std::vector<std::string> failures;
    const auto               t0 = std::chrono::steady_clock::now();
    CB::engine::exec::ParallelFor(units.size(), [&](std::size_t i) {
        CB::engine::decompile::ParsedHky g;
        std::string                      why;
        if (doc.MergedGraph(units[i], g, &why)) { ++merged; return; }
        const std::lock_guard lock(failMutex);
        failures.push_back(units[i] + ": " + why);
    });
    std::sort(failures.begin(), failures.end());
    for (const auto& f : failures) std::printf("  FAIL %s\n", f.c_str());
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: loadorder: %zu of %zu unit(s) merged in %.1f s.\n", failures.empty() ? "OK" : "FAIL", merged.load(),
                units.size(), secs);
    return failures.empty() ? 0 : 1;
}

/// @brief `merge-check`: every unit in `bundle` merged onto its base in `base` (a patch unit) or alone (a unit the
///        bundle owns), then serialized: the compile the game's warm-up runs, offline. Each result is written to
///        `outDir/<unit>` (the .hkx) and decompiled back beside it as `<unit>.merged/` for inspection.
/// @note  Bones come from the base's humanoid skeleton, so the check covers graphs on that rig.
int doMergeCheck(const std::string& basePath, const std::string& bundlePath, const std::string& schemaDir,
                 const std::string& outDir) {
    if (basePath.empty() || bundlePath.empty() || schemaDir.empty() || outDir.empty()) {
        std::printf("usage: hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    using CB::engine::codec::HkyArchive;
    using CB::engine::codec::resource::PathKey;
    CB::engine::schema::SetSharedSchemaDir(schemaDir);
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", CB::engine::schema::SharedRegistryError().c_str()); return 1; }

    std::string err;
    const auto base = HkyArchive::LoadFromFile(basePath, err);
    if (!base) { std::printf("FAIL: base: %s\n", err.c_str()); return 1; }
    const auto bundle = HkyArchive::LoadFromFile(bundlePath, err);
    if (!bundle) { std::printf("FAIL: bundle: %s\n", err.c_str()); return 1; }

    std::vector<std::string> bones;
    {
        const std::string humanoid = "meshes/actors/character/character assets/skeleton.hkx";
        if (const auto src = base->source(humanoid))
            if (auto names = CB::engine::decompile::ReadSkeletonBones(*src, *reg, humanoid)) bones = std::move(*names);
    }
    if (bones.empty()) { std::printf("FAIL: no humanoid skeleton bone list in the base\n"); return 1; }

    std::string stem = fs::path(bundlePath).stem().string();
    stem = PathKey(stem);
    std::vector<std::string> baseUnits;
    for (const auto& u : base->units()) baseUnits.push_back(u.prefix);
    std::sort(baseUnits.begin(), baseUnits.end());

    std::size_t ok = 0, failed = 0;
    for (const auto& u : bundle->units()) {
        std::vector<CB::engine::common::LayerSource> layers;
        const bool patch = std::binary_search(baseUnits.begin(), baseUnits.end(), u.prefix);
        if (patch) layers.push_back({ base->source(u.prefix), std::string(CB::engine::codec::formid::BASE_GAME_STEM),
                                      CB::engine::codec::formid::BuildMasterTable(CB::engine::codec::formid::BASE_GAME_STEM, {}, true) });
        layers.push_back({ bundle->source(u.prefix), stem, CB::engine::codec::formid::BuildMasterTable(stem, {}, true) });

        CB::engine::decompile::ParsedHky graph;
        std::vector<std::uint8_t>        bytes;
        const fs::path                   dst = fs::path(outDir) / u.prefix;
        if (!CB::engine::decompile::MergeLayersReflective(layers, *reg, &bones, graph, err) ||
            !CB::engine::decompile::SerializeGraph(graph, bytes, err) ||
            !CB::engine::disk::WriteFile(dst, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), {}, &err) ||
            !CB::engine::decompile::DecompileBehaviorReflective(bytes, *reg, dst.string() + ".merged", err, &bones)) {
            std::printf("  FAIL %s (%s): %s\n", u.prefix.c_str(), patch ? "patch" : "own unit", err.c_str());
            ++failed;
            continue;
        }
        std::printf("  OK   %s (%s): %zu object(s), %zu bytes\n", u.prefix.c_str(), patch ? "patch onto the base" : "own unit",
                    graph.objects.size(), bytes.size());
        ++ok;
    }
    std::printf("%s: merge-check '%s': %zu unit(s) merged and serialized, %zu failed.\n", failed ? "FAIL" : "OK",
                bundlePath.c_str(), ok, failed);
    return failed ? 1 : 0;
}

/// @brief `mm-sample`: the motion-matching bundle generator over a spec exercising every shape it writes (two databases
///        sharing a clip, flags, literal and expression biases, tags, chooser rows over bool, int and float variables,
///        a row that always holds, a fallback, an extra variable, two characters), its files written to `outDir`.
/// @note  The generator itself proves the behavior unit parses back to the graph it built; OK here means it did.
int doMmSample(const std::string& schemaDir, const std::string& outDir) {
    if (schemaDir.empty() || outDir.empty()) {
        std::printf("usage: hky-tool mm-sample <schemaDir> -o <outDir>\n");
        return 2;
    }
    namespace mm = CB::engine::compile;
    CB::engine::schema::SetSharedSchemaDir(schemaDir);
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", CB::engine::schema::SharedRegistryError().c_str()); return 1; }

    const std::string dir = "meshes/actors/character/animations/MotionMatching/";
    mm::MmBundleSpec  spec;
    spec.databases.push_back({ "Idle", { { dir + "idle/Idle_Loop.hkx", "", mm::kMmLooping },
                                         { dir + "idle/TurnInPlace_L.hkx", "", mm::kMmDisableReselection } },
                               "", "-0.05", "", { "idle" } });
    spec.databases.push_back({ "Run", { { dir + "running/Run_Fwd_Loop.hkx", "", mm::kMmLooping },
                                        { dir + "idle/Idle_Loop.hkx", "", mm::kMmLooping } },
                               "0.1", "", "(GraphVar('MM_Speed') > 300) * -0.02", { "run", "locomotion" } });
    spec.rows.push_back({ "GraphVar(\"MM_Gait\") == 2 && GraphVar(\"MM_IsStarting\")", { "Run" } });
    spec.rows.push_back({ "GraphVar('MM_Speed') >= 0 && GraphVar('MM_Speed') <= 50", { "Idle", "Run" } });
    spec.rows.push_back({ "", { "Idle" } });
    spec.fallback = { "Idle" };
    spec.extraVariables.push_back({ "MM_Speed", "VARIABLE_TYPE_REAL", 0 });
    spec.characters = { { "meshes/actors/character/characters/defaultmale.hkx", "0001x000b" },
                        { "meshes/actors/character/characters female/defaultfemale.hkx", "0001x000b" } };

    mm::MmFiles files;
    std::string err;
    if (!mm::BuildMotionMatchingBundle(spec, *reg, files, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    for (const auto& [path, text] : files)
        if (!CB::engine::disk::WriteFile(std::filesystem::path(outDir) / path, text, {}, &err)) {
            std::printf("FAIL: %s\n", err.c_str());
            return 1;
        }
    std::printf("OK: mm-sample -> %s: %zu file(s); the behavior unit parsed back to the graph it was built from.\n",
                outDir.c_str(), files.size());
    return 0;
}

void usage() {
    std::printf("usage:\n"
                "  hky-tool pack        <bundleDir> -o <out.hky>\n"
                "  hky-tool unpack      <in.hky>    -o <outDir>\n"
                "  hky-tool bsa-list    <in.bsa>    [prefix]\n"
                "  hky-tool bsa-extract <in.bsa>    -o <outDir> [prefix]\n"
                "  hky-tool assemble    <featureDir> <injectables.json> [schemaDir] -o <out.hky>\n"
                "  hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>\n"
                "  hky-tool embedded-roundtrip <in.hky> <schemaDir>\n"
                "  hky-tool loadorder   <schemaDir> (--mo2 <instance> [--profile <p>] | --data <Data>) [--unit <key>]... [--all]\n"
                "  hky-tool mm-sample   <schemaDir> -o <outDir>\n"
                "  hky-tool graph-html  <schemaDir> -o <out.html> (--mo2 <instance> [--profile <p>] | --plugins <dir> | --bundle <f.hky>)...\n"
                "                       [--loadorder <file>] [--unit <path>]... [--all]\n");
}

}  // namespace

/// @brief `graph-html` (GraphHtml.cpp): its own argument parser, for the repeatable --plugins / --bundle / --unit.
int doGraphHtml(int argc, char** argv);

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string verb = argv[1];
    if (verb == "graph-html") return doGraphHtml(argc - 2, argv + 2);
    if (verb == "loadorder") return doLoadOrder(argc - 2, argv + 2);

    std::string arg, arg2, arg3, out;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { out = argv[++i]; }
        else if (arg.empty())         { arg = a; }
        else if (arg2.empty())        { arg2 = a; }
        else if (arg3.empty())        { arg3 = a; }
    }

    if (verb == "pack")        return doHkyPack(arg, out);
    if (verb == "unpack")      return doHkyUnpack(arg, out);
    if (verb == "bsa-list")    return doBsaList(arg, arg2);
    if (verb == "bsa-extract") return doBsaExtract(arg, out, arg2);
    if (verb == "assemble")    return doAssemble(arg, arg2, arg3, out);
    if (verb == "merge-check") return doMergeCheck(arg, arg2, arg3, out);
    if (verb == "embedded-roundtrip") return doEmbeddedRoundTrip(arg, arg2);
    if (verb == "mm-sample")   return doMmSample(arg, out);
    usage();
    return 2;
}
