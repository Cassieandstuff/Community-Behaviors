#include "havok/sct/AnimDataFromBehavior.h"

#include "havok/anim/AnimationData.h"          // animdata::ProjectPatch / PatchAddition / ...
#include "havok/model/BehaviorData.h"
#include "havok/model/defs/GeneratorDefs.h"

#include "havok/classes/Animation.h"          // hkaAnimation (duration + annotation tracks)
#include "havok/core/PackFileDeserializer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
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
    AnimClipInfo out;
    if (bytes.empty()) return out;
    try {
        PackFileDeserializer des;
        BinaryReaderEx       br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        des.DeserializePartially(br);
        BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
        for (const auto& [off, cls] : des.ListObjects()) {
            if (cls != "hkaSplineCompressedAnimation" &&
                cls != "hkaInterleavedUncompressedAnimation" && cls != "hkaAnimation")
                continue;
            std::shared_ptr<IHavokObject> obj;
            try { obj = des.ConstructVirtualClass(dr, off); } catch (...) { break; }
            if (auto anim = std::dynamic_pointer_cast<hkaAnimation>(obj)) {
                out.has      = true;
                out.duration = static_cast<double>(anim->m_duration);
                if (!anim->m_annotationTracks.empty())
                    for (const auto& a : anim->m_annotationTracks[0].m_annotations)
                        out.annotations.emplace_back(TrimAnn(a.m_text), static_cast<double>(a.m_time));
            }
            break;  // one animation per file
        }
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
