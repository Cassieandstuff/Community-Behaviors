#include <compile/AnimDataFromBehavior.h>

#include "interface/AnimationData.h"          // animdata::ProjectPatch / PatchAddition / ...
#include "havok/model/BehaviorData.h"
#include "havok/model/defs/GeneratorDefs.h"

#include <codec/serialization/packfile/PackFileDeserializer.h>

#include <codec/serialization/HavokIo.h>          // io::SchemaObject + MakeSchemaFactory (schema-native anim read)
#include <havok-schema/HavokSchema.h>  // schema::SharedRegistry

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace havok::sct {

namespace {
    // Trim an annotation's text — byte-identical to the offline StripLine (trailing space/tab/
    // CR/LF; leading space/tab only), so the shared extractor reproduces the derive exactly.
    std::string TrimAnn(std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        std::size_t b = 0;
        while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
        return s.substr(b);
    }

    // Schema-object field readers (mirror AnimationDecompiler.cpp — no typed hka* classes).
    std::shared_ptr<havok::io::SchemaObject> asSO(const std::shared_ptr<IHavokObject>& o) {
        return std::dynamic_pointer_cast<havok::io::SchemaObject>(o);
    }
    float rdF32(havok::io::SchemaObject& o, const char* n) {
        const auto& r = o.FieldRef(n).raw; float v = 0.f;
        if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v;
    }
}  // namespace

std::vector<havok::animdata::DeriveClipInput> DeriveClipInputsFromBehavior(
    const havok::model::BehaviorData& data)
{
    std::vector<havok::animdata::DeriveClipInput> out;
    out.reserve(data.clips.size());

    for (const auto& [key, clip] : data.clips) {
        havok::animdata::DeriveClipInput dc;
        dc.name          = clip.name.empty() ? key : clip.name;
        dc.animationName = clip.animationName;
        dc.playbackSpeed = std::atof(clip.playbackSpeed.c_str());
        dc.cropStart     = std::atof(clip.cropStartAmountLocalTime.c_str());
        dc.cropEnd       = std::atof(clip.cropEndAmountLocalTime.c_str());

        // The clip generator's OWN triggers (authored on the node). Annotation triggers are
        // the caller's job (they come from the animation file, not the graph). An event with
        // no resolved name is skipped — it contributes nothing to the cache text.
        if (clip.triggers)
            for (const auto& t : *clip.triggers) {
                if (!t.event || t.event->empty()) continue;
                havok::animdata::DeriveClipInput::Trigger tr;
                tr.event               = *t.event;
                tr.localTime           = std::atof(t.localTime.c_str());
                tr.relativeToEndOfClip = t.relativeToEndOfClip;
                tr.fromAnnotation      = false;
                dc.triggers.push_back(std::move(tr));
            }

        out.push_back(std::move(dc));
    }
    return out;
}

AnimClipInfo ExtractAnimClipInfo(const std::vector<std::uint8_t>& bytes)
{
    using havok::io::SchemaObject;
    AnimClipInfo out;
    if (bytes.empty()) return out;

    // SCHEMA-NATIVE (no typed hka* classes): deserialize through havok-io's generic SchemaObject
    // graph (MakeSchemaFactory over the shared registry), the SAME path AnimationDecompiler uses.
    // Walk hkRootLevelContainer.namedVariants -> hkaAnimationContainer -> animations[0] (the spline),
    // and read duration + annotationTracks[0] off its tagged FieldValue store. Robust to the unported
    // hkMemoryResourceContainer second variant (it deserializes cleanly instead of throwing the way the
    // old typed ConstructVirtualClass walk did).
    try {
        schema::SchemaRegistry* reg = schema::SharedRegistry();
        if (!reg) return out;

        PackFileDeserializer des;
        BinaryReaderEx       br(bytes);
        des.ObjectFactory = havok::io::MakeSchemaFactory(*reg);
        auto root = asSO(des.Deserialize(br));
        if (!root) return out;

        std::shared_ptr<SchemaObject> container;
        for (auto& nvObj : root->FieldRef("namedVariants").objs) {
            auto nv = asSO(nvObj);
            if (nv && nv->FieldRef("className").str == "hkaAnimationContainer") {
                container = asSO(nv->FieldRef("variant").obj);
                break;
            }
        }
        if (!container) return out;

        auto& anims = container->FieldRef("animations").objs;
        if (anims.empty()) return out;
        auto spline = asSO(anims[0]);
        if (!spline) return out;

        // Accept the same animation classes the typed path did (all derive hkaAnimation's
        // duration + annotationTracks fields).
        const std::string cls = spline->ClassName();
        if (cls != "hkaSplineCompressedAnimation" &&
            cls != "hkaInterleavedUncompressedAnimation" && cls != "hkaAnimation")
            return out;

        out.has      = true;
        out.duration = static_cast<double>(rdF32(*spline, "duration"));

        auto& annTracks = spline->FieldRef("annotationTracks").objs;
        if (!annTracks.empty())
            if (auto at0 = asSO(annTracks[0]))
                for (auto& aObj : at0->FieldRef("annotations").objs)
                    if (auto a = asSO(aObj))
                        out.annotations.emplace_back(TrimAnn(a->FieldRef("text").str),
                                                     static_cast<double>(rdF32(*a, "time")));
    } catch (...) {}
    return out;
}

std::vector<havok::animdata::ClipGenerator> DeriveProjectClipList(
    std::vector<havok::animdata::DeriveClipInput>&                       clips,
    const std::vector<std::string>&                                     roster,
    std::unordered_map<int, double>&                                    motionDur,
    const std::function<std::vector<std::uint8_t>(const std::string&)>& readAnim,
    std::vector<std::string>*                                           unresolved)
{
    const auto low = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::unordered_map<std::string, int> rosterIdx;
    for (int i = 0; i < static_cast<int>(roster.size()); ++i)
        rosterIdx.emplace(low(roster[static_cast<std::size_t>(i)]), i);

    // Merge each clip's animation annotations + duration (each distinct animation read once).
    std::unordered_map<std::string, AnimClipInfo> animCache;
    for (auto& dc : clips) {
        if (dc.animationName.empty()) continue;
        const std::string k = low(dc.animationName);
        auto it = animCache.find(k);
        if (it == animCache.end())
            it = animCache.emplace(k, ExtractAnimClipInfo(readAnim(dc.animationName))).first;
        const AnimClipInfo& info = it->second;
        for (const auto& [text, time] : info.annotations)
            dc.triggers.push_back({ text, time, /*relativeToEndOfClip*/ false, /*fromAnnotation*/ true });
        if (info.has)
            if (auto ri = rosterIdx.find(k); ri != rosterIdx.end())
                motionDur[ri->second] = info.duration;
    }

    return havok::animdata::DeriveClipList(clips, roster, motionDur, unresolved);
}

havok::animdata::Project DeriveProjectAnimData(
    const std::string&                                                  projectName,
    std::vector<havok::animdata::DeriveClipInput>&                      clips,
    const std::vector<std::string>&                                     roster,
    const std::vector<havok::animdata::MotionRecord>&                   motions,
    const std::function<std::vector<std::uint8_t>(const std::string&)>& readAnim)
{
    std::unordered_map<int, double> motionDur;
    for (const auto& m : motions) motionDur[std::atoi(m.animIndex.c_str())] = std::atof(m.duration.c_str());

    havok::animdata::Project proj;
    proj.name        = projectName.size() >= 4 && projectName.compare(projectName.size() - 4, 4, ".txt") == 0
                           ? projectName : projectName + ".txt";
    proj.fieldX      = "1";
    proj.hasAnimData = true;
    proj.clips       = DeriveProjectClipList(clips, roster, motionDur, readAnim);
    proj.motions     = motions;
    return proj;
}

havok::animdata::ProjectPatch DeriveProjectPatch(
    const std::string&                                                  projectName,
    std::vector<havok::animdata::DeriveClipInput>&                      clips,
    const std::string&                                                  symbolCode,
    const std::function<std::vector<std::uint8_t>(const std::string&)>& readAnim,
    const std::unordered_map<std::string, havok::animdata::MotionRecord>* animMotions)
{
    // Synthetic roster = each clip's own animationName, so DeriveProjectClipList resolves every
    // clip to a distinct slot (and reads its anim once for duration + annotation triggers) WITHOUT
    // needing the character's real roster. We discard that resolved index — the merge assigns the
    // real (high-band) one from the symbol.
    std::vector<std::string> synthRoster;
    synthRoster.reserve(clips.size());
    for (const auto& dc : clips) synthRoster.push_back(dc.animationName);

    // animationName normalized to the key form animMotions uses (lowercase, '\\'->'/').
    const auto normAnim = [](std::string s) {
        for (char& c : s) c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    std::unordered_map<int, double> motionDur;
    const auto records = DeriveProjectClipList(clips, synthRoster, motionDur, readAnim, nullptr);

    const auto fmtG = [](double v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return std::string(buf);
    };

    havok::animdata::ProjectPatch pp;
    pp.projectName = projectName;
    int n = 0;
    for (const auto& cg : records) {
        const std::string symbol   = symbolCode + "$" + std::to_string(++n);
        const int         synthIdx = std::atoi(cg.animIndex.c_str());
        const double      dur      = (synthIdx >= 0 && motionDur.count(synthIdx)) ? motionDur[synthIdx] : 0.0;

        havok::animdata::PatchAddition add;
        add.symbol         = symbol;
        add.clip           = cg;        // canonical record (name / crop / playback / triggers)
        add.clip.animIndex = symbol;    // defer the real index to MergeProjectPatch's high band
        add.hasClip        = true;

        // Real root motion when this clip's animation authored one (BR-native animation.yaml `motion:`),
        // else the FNIS-style in-place placeholder (one zero translation + identity rotation at the
        // clip's duration). Either way the animIndex is the symbol — MergeProjectPatch pairs clip+motion.
        const havok::animdata::MotionRecord* real = nullptr;
        if (animMotions && synthIdx >= 0 && static_cast<std::size_t>(synthIdx) < synthRoster.size()) {
            const auto it = animMotions->find(normAnim(synthRoster[synthIdx]));
            if (it != animMotions->end()) real = &it->second;
        }
        if (real) {
            add.motion.duration     = real->duration;
            add.motion.translations = real->translations;
            add.motion.rotations    = real->rotations;
        } else {
            add.motion.duration     = fmtG(dur);
            add.motion.translations = { fmtG(dur) + " 0 0 0" };
            add.motion.rotations    = { "1 0 0 0 1" };
        }
        add.motion.animIndex = symbol;
        add.hasMotion        = true;

        pp.additions.push_back(std::move(add));
    }
    return pp;
}

}  // namespace havok::sct
