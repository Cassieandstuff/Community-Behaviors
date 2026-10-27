#include "codec/format/AnimationYamlLoader.h"

#include "interface/MotionSample.h"   // animdata::UnlabelSample (byte-identical motion-sample parse)

#include <RymlInclude.h>
#include <decompile/BehaviorDecompile.h>
#include <disk/Disk.h>
#include <text/Text.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace CB::engine::anim {
namespace {

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

/// @brief The unit's `curves:`, read through the schema (CBAnimationCurves).
/// @throws std::runtime_error when they do not parse, a name is empty or repeated, keys run backwards in time, or an
///         interpolation is not one AnimCurveKey knows: a curve read wrong would steer its readers silently.
std::vector<AnimCurveDef> readCurves(const c4::yml::ConstNodeRef& a, const std::string& sourceName) {
    const auto bad = [&](const std::string& why) {
        return std::runtime_error("AnimationYamlLoader: bad curves in '" + sourceName + "': " + why);
    };
    schema::SchemaRegistry* reg = schema::SharedRegistry();
    if (!reg) throw bad("no schema registry (" + schema::SharedRegistryError() + ")");
    std::string err;
    const auto  obj = decompile::ParseEmbeddedObject(a, "CBAnimationCurves", *reg, sourceName, err);
    if (!obj) throw bad(err);
    std::vector<AnimCurveDef> out;
    std::set<std::string>     names;
    for (const auto& p : obj->FieldRef("curves").objs) {
        auto* c = dynamic_cast<codec::io::SchemaObject*>(p.get());
        if (!c) throw bad("a curve is not an inline CBAnimationCurve");
        AnimCurveDef def{ c->Str("name"), {} };
        if (def.name.empty()) throw bad("a curve has no name");
        if (!names.insert(def.name).second) throw bad("'" + def.name + "' is defined twice");
        for (const auto& kp : c->FieldRef("keys").objs) {
            auto* k = dynamic_cast<codec::io::SchemaObject*>(kp.get());
            if (!k) throw bad("'" + def.name + "' has a key that is not an inline CBCurveKey");
            const std::int64_t interp = k->GetInt("interp");
            if (interp != 0 && interp != 1) throw bad("'" + def.name + "' has an unknown interp " + std::to_string(interp));
            AnimCurveKey key{ k->GetFloat("time"), k->GetFloat("value"), static_cast<AnimCurveKey::Interp>(interp) };
            if (!def.keys.empty() && key.time < def.keys.back().time)
                throw bad("'" + def.name + "' has keys out of time order at " + std::to_string(key.time));
            def.keys.push_back(key);
        }
        out.push_back(std::move(def));
    }
    return out;
}

} // namespace

AnimationDef AnimationYamlLoader::LoadFromString(const std::string& yamlText, const std::string& sourceName) {
    std::string text(CB::engine::text::StripBom(yamlText));   // parse_in_place mutates the buffer; the tree references
                                                              // it, so this local must outlive the build below (it does).
    if (text.empty())
        throw std::runtime_error("AnimationYamlLoader: empty animation source '" + sourceName + "'");

    common::EnsureRymlThrows();
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

    if (hasChild(a, "motionTarget")) {
        const std::string t = strf(a, "motionTarget");
        if (t == "motionMatching")     def.motionTarget = AnimationDef::MotionTarget::MotionMatching;
        else if (t == "animationData") def.motionTarget = AnimationDef::MotionTarget::AnimationData;
        else throw std::runtime_error("AnimationYamlLoader: '" + sourceName + "': motionTarget '" + t +
                                      "' is neither animationData nor motionMatching");
    }

    if (hasChild(a, "preload")) {
        const std::string p = strf(a, "preload");
        if (p == "true")       def.preload = true;
        else if (p == "false") def.preload = false;
        else throw std::runtime_error("AnimationYamlLoader: '" + sourceName + "': preload '" + p + "' is neither true nor false");
    }

    if (hasChild(a, "curves")) def.curves = readCurves(a, sourceName);

    return def;
}

std::string EmitAnimTrackYaml(const AnimTrackDef& tr) {
    using text::FormatFloat;
    std::string y = "    - bone: " + text::YamlScalar(tr.bone) + "\n";
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

std::string EmitAnimCurvesYaml(const std::vector<AnimCurveDef>& curves) {
    if (curves.empty()) return {};
    schema::SchemaRegistry* reg = schema::SharedRegistry();
    if (!reg) throw std::runtime_error("EmitAnimCurvesYaml: no schema registry (" + schema::SharedRegistryError() + ")");
    const auto make = [&](const char* cls) {
        const schema::ClassSchema* cs = reg->Find(cls);
        if (!cs) throw std::runtime_error(std::string("EmitAnimCurvesYaml: the schema has no ") + cls);
        auto so = std::make_shared<codec::io::SchemaObject>(reg, cs);
        so->Init();
        return so;
    };
    auto  root = make("CBAnimationCurves");
    auto& list = root->FieldRef("curves").objs;
    for (const auto& c : curves) {
        auto curve          = make("CBAnimationCurve");
        curve->Str("name")  = c.name;
        auto& keys          = curve->FieldRef("keys").objs;
        for (const auto& k : c.keys) {
            auto key = make("CBCurveKey");
            key->SetFloat("time", k.time);
            key->SetFloat("value", k.value);
            key->SetInt("interp", static_cast<std::int64_t>(k.interp));
            keys.push_back(std::move(key));
        }
        list.push_back(std::move(curve));
    }
    std::string out;
    const std::string body = decompile::EmitEmbeddedObject(*root, *reg);
    for (std::size_t at = 0; at < body.size();) {
        const std::size_t end = (std::min)(body.find('\n', at), body.size());
        out += "  " + body.substr(at, end - at) + "\n";
        at = end + 1;
    }
    return out;
}

} // namespace CB::engine::anim
