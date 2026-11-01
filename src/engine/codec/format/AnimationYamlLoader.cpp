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
using codec::io::SchemaObject;

SchemaObject* asSchema(const std::shared_ptr<codec::IHavokObject>& p) { return dynamic_cast<SchemaObject*>(p.get()); }

std::shared_ptr<SchemaObject> make(const schema::SchemaRegistry& reg, const char* cls) {
    const schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) throw std::runtime_error(std::string("AnimationYamlLoader: the schema has no ") + cls);
    auto so = std::make_shared<SchemaObject>(&reg, cs);
    so->Init();
    return so;
}

schema::SchemaRegistry& registry(const std::string& where) {
    schema::SchemaRegistry* reg = schema::SharedRegistry();
    if (!reg) throw std::runtime_error("AnimationYamlLoader: no schema registry for '" + where + "' (" + schema::SharedRegistryError() + ")");
    return *reg;
}

/// @brief The unit's curves.
/// @throws std::runtime_error when a name is empty or repeated, keys run backwards in time, or an interpolation is not
///         one AnimCurveKey knows: a curve read wrong would steer its readers silently.
std::vector<AnimCurveDef> readCurves(const SchemaObject& a, const std::string& sourceName) {
    const auto bad = [&](const std::string& why) {
        return std::runtime_error("AnimationYamlLoader: bad curves in '" + sourceName + "': " + why);
    };
    std::vector<AnimCurveDef> out;
    std::set<std::string>     names;
    for (const auto& p : a.Children("curves")) {
        auto* c = asSchema(p);
        if (!c) throw bad("a curve is not an inline CBAnimationCurve");
        AnimCurveDef def{ c->Str("name"), {} };
        if (def.name.empty()) throw bad("a curve has no name");
        if (!names.insert(def.name).second) throw bad("'" + def.name + "' is defined twice");
        for (const auto& kp : c->Children("keys")) {
            auto* k = asSchema(kp);
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

template <int N, class Key>
std::vector<Key> readKeys(const SchemaObject& track, const char* field) {
    std::vector<Key> out;
    for (const auto& p : track.Children(field)) {
        const auto* k = asSchema(p);
        if (!k) continue;
        Key key;
        key.time = k->GetFloat("time");
        for (int i = 0; i < N; ++i) key.value[i] = k->GetFloat("value", i);
        out.push_back(key);
    }
    return out;
}

template <int N, class Key>
void writeKeys(const schema::SchemaRegistry& reg, SchemaObject& track, const char* field, const char* cls, const std::vector<Key>& keys) {
    auto& list = track.FieldRef(field).objs;
    for (const auto& key : keys) {
        auto k = make(reg, cls);
        k->SetFloat("time", key.time);
        for (int i = 0; i < N; ++i) k->SetFloat("value", key.value[i], i);
        list.push_back(std::move(k));
    }
}

/// @brief `def` as a CBAnimation.
/// @note  The inherited counts are written from the tracks. An inline struct left unset still renders (zeroed), so
///        `compression` is always written; `motion` only when authored (unset, its empty record reads back as none).
std::shared_ptr<SchemaObject> toSchema(const AnimationDef& def, const schema::SchemaRegistry& reg) {
    auto a = make(reg, "CBAnimation");
    a->Str("name") = def.name;
    a->SetFloat("duration", def.duration);
    a->SetInt("numberOfTransformTracks", static_cast<std::int64_t>(def.tracks.size()));
    a->SetInt("numberOfFloatTracks", static_cast<std::int64_t>(def.floatTracks.size()));
    a->SetInt("numFrames", def.numFrames);
    a->SetFloat("frameDuration", def.frameDuration);
    a->Str("skeleton") = def.skeleton;
    {
        auto c = make(reg, "CBAnimationCompression");
        c->SetFloat("rotationTolerance", def.compression.rotationTolerance);
        c->SetFloat("translationTolerance", def.compression.translationTolerance);
        c->SetFloat("scaleTolerance", def.compression.scaleTolerance);
        c->SetInt("rotationDegree", def.compression.rotationDegree);
        c->SetInt("translationDegree", def.compression.translationDegree);
        c->SetInt("scaleDegree", def.compression.scaleDegree);
        c->SetInt("maxFramesPerBlock", def.compression.maxFramesPerBlock);
        a->FieldRef("compression").obj = std::move(c);
    }
    for (const auto& t : def.tracks) {
        auto tr         = make(reg, "CBAnimationTrack");
        tr->Str("bone") = t.bone;
        writeKeys<3>(reg, *tr, "translation", "CBVec3Key", t.translation);
        writeKeys<4>(reg, *tr, "rotation", "CBQuatKey", t.rotation);
        writeKeys<3>(reg, *tr, "scale", "CBVec3Key", t.scale);
        a->FieldRef("tracks").objs.push_back(std::move(tr));
    }
    for (const auto& ft : def.floatTracks) {
        auto f         = make(reg, "CBAnimationFloatTrack");
        f->Str("name") = ft.name;
        for (const auto& k : ft.keyframes) {
            auto key = make(reg, "CBFloatKey");
            key->SetFloat("time", k.time);
            key->SetFloat("value", k.value);
            f->FieldRef("keyframes").objs.push_back(std::move(key));
        }
        a->FieldRef("floatTracks").objs.push_back(std::move(f));
    }
    for (const auto& at : def.annotationTracks) {
        auto t              = make(reg, "hkaAnnotationTrack");
        t->Str("trackName") = at.trackName;
        for (const auto& ann : at.annotations) {
            auto e = make(reg, "hkaAnnotationTrackAnnotation");
            e->SetFloat("time", ann.time);
            e->Str("text") = ann.text;
            t->FieldRef("annotations").objs.push_back(std::move(e));
        }
        a->FieldRef("annotationTracks").objs.push_back(std::move(t));
    }
    if (def.motion) {
        auto m             = make(reg, "CBAnimationMotion");
        m->Str("duration") = def.motion->duration;
        for (const auto& s : def.motion->translations) m->FieldRef("translation").strs.push_back(animdata::LabelSample(s));
        for (const auto& s : def.motion->rotations) m->FieldRef("rotation").strs.push_back(animdata::LabelSample(s));
        a->FieldRef("motion").obj = std::move(m);
    }
    for (const auto& c : def.curves) {
        auto curve         = make(reg, "CBAnimationCurve");
        curve->Str("name") = c.name;
        for (const auto& k : c.keys) {
            auto key = make(reg, "CBCurveKey");
            key->SetFloat("time", k.time);
            key->SetFloat("value", k.value);
            key->SetInt("interp", static_cast<std::int64_t>(k.interp));
            curve->FieldRef("keys").objs.push_back(std::move(key));
        }
        a->FieldRef("curves").objs.push_back(std::move(curve));
    }
    a->SetInt("motionTarget", def.motionTarget == AnimationDef::MotionTarget::MotionMatching ? 1 : 0);
    a->SetBool("rootMotionVisual", def.rootMotionVisual);
    a->SetBool("preload", def.preload);
    return a;
}

AnimationDef fromSchema(const SchemaObject& a, const std::string& sourceName) {
    AnimationDef def;
    def.name          = a.Str("name");
    def.duration      = a.GetFloat("duration");
    def.numFrames     = static_cast<int>(a.GetInt("numFrames"));
    def.frameDuration = a.GetFloat("frameDuration");
    def.skeleton      = a.Str("skeleton");
    /// @note An absent key parses as a zeroed struct (maxFramesPerBlock 0 is no setting): the defaults hold.
    if (const auto* c = a.Child("compression"); c && c->GetInt("maxFramesPerBlock") != 0) {
        def.compression.rotationTolerance    = c->GetFloat("rotationTolerance");
        def.compression.translationTolerance = c->GetFloat("translationTolerance");
        def.compression.scaleTolerance       = c->GetFloat("scaleTolerance");
        def.compression.rotationDegree       = static_cast<int>(c->GetInt("rotationDegree"));
        def.compression.translationDegree    = static_cast<int>(c->GetInt("translationDegree"));
        def.compression.scaleDegree          = static_cast<int>(c->GetInt("scaleDegree"));
        def.compression.maxFramesPerBlock    = static_cast<int>(c->GetInt("maxFramesPerBlock"));
    }
    for (const auto& p : a.Children("tracks")) {
        const auto* t = asSchema(p);
        if (!t) continue;
        AnimTrackDef tr;
        tr.bone        = t->Str("bone");
        tr.translation = readKeys<3, Vec3Keyframe>(*t, "translation");
        tr.rotation    = readKeys<4, QuatKeyframe>(*t, "rotation");
        tr.scale       = readKeys<3, Vec3Keyframe>(*t, "scale");
        def.tracks.push_back(std::move(tr));
    }
    for (const auto& p : a.Children("floatTracks")) {
        const auto* t = asSchema(p);
        if (!t) continue;
        AnimFloatTrackDef ft;
        ft.name = t->Str("name");
        for (const auto& kp : t->Children("keyframes"))
            if (const auto* k = asSchema(kp)) ft.keyframes.push_back({ k->GetFloat("time"), k->GetFloat("value") });
        def.floatTracks.push_back(std::move(ft));
    }
    for (const auto& p : a.Children("annotationTracks")) {
        const auto* t = asSchema(p);
        if (!t) continue;
        AnimAnnotationTrackDef at;
        at.trackName = t->Str("trackName");
        for (const auto& ap : t->Children("annotations"))
            if (const auto* e = asSchema(ap)) at.annotations.push_back({ e->GetFloat("time"), e->Str("text") });
        def.annotationTracks.push_back(std::move(at));
    }
    if (auto* m = a.Child("motion"); m && !m->Str("duration").empty()) {
        animdata::MotionRecord mr;
        mr.duration = m->Str("duration");
        for (const auto& s : m->FieldRef("translation").strs) mr.translations.push_back(animdata::UnlabelSample(s));
        for (const auto& s : m->FieldRef("rotation").strs) mr.rotations.push_back(animdata::UnlabelSample(s));
        def.motion = std::move(mr);
    }
    def.curves = readCurves(a, sourceName);
    const std::int64_t target = a.GetInt("motionTarget");
    if (target != 0 && target != 1)
        throw std::runtime_error("AnimationYamlLoader: '" + sourceName + "': motionTarget " + std::to_string(target) +
                                 " is neither animationData nor motionMatching");
    def.motionTarget     = target == 1 ? AnimationDef::MotionTarget::MotionMatching : AnimationDef::MotionTarget::AnimationData;
    def.rootMotionVisual = a.GetBool("rootMotionVisual");
    def.preload          = a.GetBool("preload");
    /// @note Written from the tracks on emit; a unit that states other counts was edited inconsistently.
    const auto counted = [&](const char* f, std::size_t n) {
        const std::int64_t v = a.GetInt(f);
        if (v != 0 && v != static_cast<std::int64_t>(n))
            throw std::runtime_error("AnimationYamlLoader: '" + sourceName + "': " + f + " " + std::to_string(v) + " but " +
                                     std::to_string(n) + " are listed");
    };
    counted("numberOfTransformTracks", def.tracks.size());
    counted("numberOfFloatTracks", def.floatTracks.size());
    return def;
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
    const c4::yml::id_type at = hasChild(root, "animation") ? root["animation"].id() : root.id();

    /// @note The legacy `motion: none` (units written before CBAnimation) is the schema's rootMotionVisual.
    bool visual = false;
    if (const auto a = tree.cref(at); hasChild(a, "motion") && a["motion"].has_val()) {
        visual = strf(a, "motion") == "none";
        tree.remove(a["motion"].id());
    }

    std::string err;
    const auto  obj = decompile::ParseEmbeddedObject(tree.cref(at), "CBAnimation", registry(sourceName), sourceName, err);
    if (!obj) throw std::runtime_error("AnimationYamlLoader: '" + sourceName + "': " + err);
    AnimationDef def     = fromSchema(*obj, sourceName);
    def.rootMotionVisual = def.rootMotionVisual || visual;
    return def;
}

std::string EmitAnimationYaml(const AnimationDef& def) {
    const schema::SchemaRegistry& reg = registry(def.name);
    std::string       out  = "animation:\n";
    const std::string body = decompile::EmitEmbeddedObject(*toSchema(def, reg), reg);
    for (std::size_t at = 0; at < body.size();) {
        const std::size_t end = (std::min)(body.find('\n', at), body.size());
        out += "  " + body.substr(at, end - at) + "\n";
        at = end + 1;
    }
    return out;
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

AnimationDef AnimationYamlLoader::Load(const std::filesystem::path& yamlFile) {
    std::string text = disk::ReadFile(yamlFile, { .stripBom = true }).value_or(std::string());
    if (text.empty())
        throw std::runtime_error("AnimationYamlLoader: cannot read " + yamlFile.string());
    return LoadFromString(text, yamlFile.string());
}

} // namespace CB::engine::anim
