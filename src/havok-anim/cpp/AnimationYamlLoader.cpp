#include "havok/anim/AnimationYamlLoader.h"

#include "havok/anim/AnimDataYaml.h"   // animdata::UnlabelSample (byte-identical motion-sample parse)

#include <RymlInclude.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace havok::anim {
namespace {

std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    return s;
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
    std::string text = yamlText;   // parse_in_place mutates the buffer; the tree references it, so this
                                   // local must outlive the build below (it does — same scope).
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    if (text.empty())
        throw std::runtime_error("AnimationYamlLoader: empty animation source '" + sourceName + "'");

    c4::yml::Tree tree;
    try { tree = c4::yml::parse_in_place(c4::to_substr(text)); }
    catch (const std::exception& e) {
        throw std::runtime_error("AnimationYamlLoader: parse error in '" + sourceName + "': " + e.what());
    }

    auto root = tree.rootref();
    auto a = hasChild(root, "animation") ? root["animation"] : root;

    AnimationDef def;
    def.name     = strf(a, "name");
    def.duration = ff(a, "duration");
    def.skeleton = strf(a, "skeleton");

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
    if (hasChild(a, "motion")) {
        auto m = a["motion"];
        havok::animdata::MotionRecord mr;
        mr.duration = strf(m, "duration");   // verbatim token (NOT reparsed to float)
        if (hasChild(m, "translation") && m["translation"].is_seq())
            for (auto t : m["translation"]) {
                if (!t.has_val()) continue;
                std::string s; c4::from_chars(t.val(), &s);
                mr.translations.push_back(havok::animdata::UnlabelSample(s));
            }
        if (hasChild(m, "rotation") && m["rotation"].is_seq())
            for (auto r : m["rotation"]) {
                if (!r.has_val()) continue;
                std::string s; c4::from_chars(r.val(), &s);
                mr.rotations.push_back(havok::animdata::UnlabelSample(s));
            }
        def.motion = std::move(mr);
    }

    return def;
}

AnimationDef AnimationYamlLoader::Load(const std::filesystem::path& yamlFile) {
    std::string text = readFile(yamlFile);
    if (text.empty())
        throw std::runtime_error("AnimationYamlLoader: cannot read " + yamlFile.string());
    return LoadFromString(text, yamlFile.string());
}

} // namespace havok::anim
