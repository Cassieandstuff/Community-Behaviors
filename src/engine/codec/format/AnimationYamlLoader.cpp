#include "codec/format/AnimationYamlLoader.h"

#include "interface/MotionSample.h"   // animdata::UnlabelSample (byte-identical motion-sample parse)

#include <RymlInclude.h>
#include <disk/Disk.h>
#include <text/Text.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace CB::engine::anim {
namespace {

// ryml's DEFAULT error handler ABORTS (fail-fast), so a malformed YAML kills the process instead of
// throwing — uncatchable, and fatal to a corpus-wide pass over generated files. Install a callback that
// THROWS, once, so parse_in_place errors become std::runtime_error the loader's try/catch can handle
// (and callers can isolate per-file). Global (ryml callbacks are process-wide) and strictly safer for
// every other schema-stack YAML parse too.
[[noreturn]] void rymlThrowBasic(c4::csubstr msg, c4::yml::ErrorDataBasic const&, void*) {
    throw std::runtime_error(std::string(msg.str, msg.len));
}
[[noreturn]] void rymlThrowParse(c4::csubstr msg, c4::yml::ErrorDataParse const&, void*) {
    throw std::runtime_error(std::string(msg.str, msg.len));
}
[[noreturn]] void rymlThrowVisit(c4::csubstr msg, c4::yml::ErrorDataVisit const&, void*) {
    throw std::runtime_error(std::string(msg.str, msg.len));
}
// c4core's OWN error path (lower-level asserts/checks inside the parser) defaults to abort/terminate,
// which the ryml-level callbacks above do NOT cover. Route it through the callback and clear the ABORT
// flag so those become catchable exceptions too.
[[noreturn]] void c4ThrowOnError(const char* msg, std::size_t sz) {
    throw std::runtime_error(std::string(msg, sz));
}
void ensureRymlThrows() {
    static const bool once = [] {
        c4::yml::Callbacks cb = c4::yml::get_callbacks();
        cb.m_error_basic = &rymlThrowBasic;
        cb.m_error_parse = &rymlThrowParse;
        cb.m_error_visit = &rymlThrowVisit;
        c4::yml::set_callbacks(cb);
        c4::set_error_flags(c4::ON_ERROR_CALLBACK);   // no abort/terminate — invoke our callback instead
        c4::set_error_callback(&c4ThrowOnError);
        return true;
    }();
    (void)once;
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
// A flow/block seq of scalars ("value: [x, y, z]") -> floats.
std::vector<float> floatSeq(const c4::yml::ConstNodeRef& parent, const char* key) {
    std::vector<float> v;
    if (!hasChild(parent, key)) return v;
    auto n = parent[c4::to_csubstr(key)];
    if (!n.readable() || !n.is_seq()) return v;
    for (auto c : n) {
        if (!c.has_val()) continue;
        std::string s; c4::from_chars(c.val(), &s);
        try { v.push_back(std::stof(s)); } catch (...) { v.push_back(0.f); }
    }
    return v;
}

} // namespace

AnimationDef AnimationYamlLoader::LoadFromString(const std::string& yamlText, const std::string& sourceName) {
    std::string text(CB::engine::text::StripBom(yamlText));   // parse_in_place mutates the buffer; the tree references
                                                              // it, so this local must outlive the build below (it does).
    if (text.empty())
        throw std::runtime_error("AnimationYamlLoader: empty animation source '" + sourceName + "'");

    ensureRymlThrows();   // make a malformed-YAML parse THROW (catchable) instead of abort the process
    c4::yml::Tree tree;
    try { tree = c4::yml::parse_in_place(c4::to_substr(text)); }
    catch (const std::exception& e) {
        throw std::runtime_error("AnimationYamlLoader: parse error in '" + sourceName + "': " + e.what());
    }

    auto root = tree.rootref();
    auto a = hasChild(root, "animation") ? root["animation"] : root;

    AnimationDef def;
    def.name          = strf(a, "name");
    def.duration      = ff(a, "duration");
    def.numFrames     = fi(a, "numFrames", 0);       // native frame count (0 = derive from fps)
    def.frameDuration = ff(a, "frameDuration", 0.f); // native frame spacing (0 = 1/fps)
    def.skeleton      = strf(a, "skeleton");

    if (hasChild(a, "compression")) {
        auto c = a["compression"];
        def.compression.rotationTolerance    = ff(c, "rotationTolerance", 0.001f);
        def.compression.translationTolerance = ff(c, "translationTolerance", 0.001f);
        def.compression.scaleTolerance       = ff(c, "scaleTolerance", 0.001f);
        def.compression.rotationDegree       = fi(c, "rotationDegree", 3);
        def.compression.translationDegree    = fi(c, "translationDegree", 1);
        def.compression.scaleDegree          = fi(c, "scaleDegree", 1);
        def.compression.maxFramesPerBlock    = fi(c, "maxFramesPerBlock", 256);
    }

    if (hasChild(a, "tracks") && a["tracks"].is_seq()) {
        for (auto t : a["tracks"]) {
            AnimTrackDef tr;
            tr.bone = strf(t, "bone");
            if (hasChild(t, "translation") && t["translation"].is_seq())
                for (auto k : t["translation"]) {
                    Vec3Keyframe kf; kf.time = ff(k, "time");
                    auto v = floatSeq(k, "value");
                    for (int i = 0; i < 3 && i < (int)v.size(); i++) kf.value[i] = v[i];
                    tr.translation.push_back(kf);
                }
            if (hasChild(t, "rotation") && t["rotation"].is_seq())
                for (auto k : t["rotation"]) {
                    QuatKeyframe kf; kf.time = ff(k, "time");
                    auto v = floatSeq(k, "value");
                    for (int i = 0; i < 4 && i < (int)v.size(); i++) kf.value[i] = v[i];
                    tr.rotation.push_back(kf);
                }
            if (hasChild(t, "scale") && t["scale"].is_seq())
                for (auto k : t["scale"]) {
                    Vec3Keyframe kf; kf.time = ff(k, "time");
                    auto v = floatSeq(k, "value");
                    for (int i = 0; i < 3 && i < (int)v.size(); i++) kf.value[i] = v[i];
                    tr.scale.push_back(kf);
                }
            def.tracks.push_back(std::move(tr));
        }
    }

    if (hasChild(a, "floatTracks") && a["floatTracks"].is_seq()) {
        for (auto t : a["floatTracks"]) {
            AnimFloatTrackDef ft;
            ft.name = strf(t, "name");
            if (hasChild(t, "keyframes") && t["keyframes"].is_seq())
                for (auto k : t["keyframes"]) {
                    FloatKeyframe kf; kf.time = ff(k, "time"); kf.value = ff(k, "value");
                    ft.keyframes.push_back(kf);
                }
            def.floatTracks.push_back(std::move(ft));
        }
    }

    if (hasChild(a, "annotationTracks") && a["annotationTracks"].is_seq()) {
        for (auto t : a["annotationTracks"]) {
            AnimAnnotationTrackDef at;
            at.trackName = strf(t, "trackName");
            if (hasChild(t, "annotations") && t["annotations"].is_seq())
                for (auto k : t["annotations"]) {
                    AnimAnnotation ann; ann.time = ff(k, "time"); ann.text = strf(k, "text");
                    at.annotations.push_back(std::move(ann));
                }
            def.annotationTracks.push_back(std::move(at));
        }
    }

    // motion: — the optional root-motion record (last, after annotationTracks). Same schema as the
    // animationdata motion sidecar (duration + translation/rotation samples), one level under `motion:`.
    // Parsed here directly (rather than re-emitting the subtree to text for ParseMotionSidecar) but kept
    // byte-identical to it by reusing animdata::UnlabelSample on every sample. animIndex/animation stay
    // empty — the compiler binds them per-project at derive time from this animation's clip.
    if (hasChild(a, "motion") && a["motion"].has_val()) {
        def.rootMotionVisual = strf(a, "motion") == "none";
    } else if (hasChild(a, "motion")) {
        auto m = a["motion"];
        CB::engine::animdata::MotionRecord mr;
        mr.duration = strf(m, "duration");   // verbatim token (NOT reparsed to float)
        if (hasChild(m, "translation") && m["translation"].is_seq())
            for (auto t : m["translation"]) {
                if (!t.has_val()) continue;
                std::string s; c4::from_chars(t.val(), &s);
                mr.translations.push_back(CB::engine::animdata::UnlabelSample(s));
            }
        if (hasChild(m, "rotation") && m["rotation"].is_seq())
            for (auto r : m["rotation"]) {
                if (!r.has_val()) continue;
                std::string s; c4::from_chars(r.val(), &s);
                mr.rotations.push_back(CB::engine::animdata::UnlabelSample(s));
            }
        def.motion = std::move(mr);
    }

    /// @note A malformed `tags:` throws rather than loading as no tags: a silently untagged clip is the failure the
    ///       build's injection exists to prevent.
    if (hasChild(a, "tags")) {
        const auto t   = a["tags"];
        const auto bad = [&](const std::string& why) {
            return std::runtime_error("AnimationYamlLoader: bad tags in '" + sourceName + "': " + why);
        };
        if (!t.is_map()) throw bad("not a map of tag name -> [[start, end], ...]");
        auto& tags = def.tags.emplace();
        for (auto tag : t) {
            std::string name;
            c4::from_chars(tag.key(), &name);
            if (!tag.is_seq()) throw bad("'" + name + "' is not a list of ranges");
            auto& ranges = tags[name];
            for (auto r : tag) {
                int  v[2] = { 0, 0 };
                bool ok   = r.is_seq() && r.num_children() == 2;
                for (int i = 0; ok && i < 2; ++i) ok = r[i].has_val() && c4::atoi(r[i].val(), &v[i]);
                if (!ok) throw bad("'" + name + "' has a range that is not [start, end]");
                ranges.emplace_back(v[0], v[1]);
            }
        }
    }

    return def;
}

std::string EmitAnimTrackYaml(const AnimTrackDef& tr) {
    using text::FormatFloat;
    std::string y = "    - bone: " + tr.bone + "\n";
    const auto vec3 = [&](const char* key, const std::vector<Vec3Keyframe>& keys) {
        if (keys.empty()) return;
        y += std::string("      ") + key + ":\n";
        for (const auto& k : keys)
            y += "        - { time: " + FormatFloat(k.time) + ", value: [" + FormatFloat(k.value[0]) + ", " +
                 FormatFloat(k.value[1]) + ", " + FormatFloat(k.value[2]) + "] }\n";
    };
    vec3("translation", tr.translation);
    if (!tr.rotation.empty()) {
        y += "      rotation:\n";
        for (const auto& k : tr.rotation)
            y += "        - { time: " + FormatFloat(k.time) + ", value: [" + FormatFloat(k.value[0]) + ", " +
                 FormatFloat(k.value[1]) + ", " + FormatFloat(k.value[2]) + ", " + FormatFloat(k.value[3]) + "] }\n";
    }
    vec3("scale", tr.scale);
    return y;
}

bool ReplaceTrackYaml(std::string& text, std::size_t trackIndex, const AnimTrackDef& track) {
    const std::string head = "\n    - bone: ";
    std::size_t at = text.find("\n  tracks:\n");
    if (at == std::string::npos) return false;
    for (std::size_t i = 0; i <= trackIndex; ++i) {
        at = text.find(head, at + 1);
        if (at == std::string::npos) return false;
    }
    /// @note The block ends at the next track or the next `animation:` key (indent 2).
    std::size_t end = text.size();
    for (std::size_t p = text.find('\n', at + 1); p != std::string::npos && p + 1 < text.size(); p = text.find('\n', p + 1)) {
        if (text.compare(p, head.size(), head) == 0 || (p + 3 < text.size() && text.compare(p + 1, 2, "  ") == 0 && text[p + 3] != ' ')) { end = p; break; }
    }
    const std::string body = EmitAnimTrackYaml(track);
    text.replace(at + 1, end - (at + 1), body.substr(0, body.size() - 1));
    return true;
}

AnimationDef AnimationYamlLoader::Load(const std::filesystem::path& yamlFile) {
    std::string text = disk::ReadFile(yamlFile, { .stripBom = true }).value_or(std::string());
    if (text.empty())
        throw std::runtime_error("AnimationYamlLoader: cannot read " + yamlFile.string());
    return LoadFromString(text, yamlFile.string());
}

} // namespace CB::engine::anim
