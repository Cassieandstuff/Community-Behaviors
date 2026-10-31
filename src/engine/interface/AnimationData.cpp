#include "interface/AnimationData.h"

#include <codec/serialization/MetaText.h>

namespace CB::engine::animdata {

    SingleFile ReadSingleFile(const schema::SchemaRegistry& reg, std::string_view text)
    {
        using codec::io::SchemaObject;
        const auto obj = [](const auto& o) -> const SchemaObject& { return static_cast<const SchemaObject&>(*o); };
        const auto str = [](const SchemaObject& o, std::string_view f) -> const std::string& { return o.FindField(f)->str; };

        std::string err;
        const auto  root = codec::io::ReadMetaText(reg, "AnimationDataSingleFile", text, err);
        if (!root) throw ParseError(err);
        SingleFile sf;
        for (const auto& po : root->Children("projects")) {
            const auto& p_ = obj(po);
            Project&    p  = sf.projects.emplace_back();
            p.name         = str(p_, "name");
            p.fieldX       = str(p_, "fieldX");
            p.assetPaths   = p_.FindField("assetPaths")->strs;
            p.hasAnimData  = str(p_, "hasAnimData") == "1";
            for (const auto& co : p_.Children("clips")) {
                const auto& c_ = obj(co);
                ClipGenerator& c = p.clips.emplace_back();
                c.name          = str(c_, "name");
                c.animIndex     = str(c_, "animIndex");
                c.playbackSpeed = str(c_, "playbackSpeed");
                c.cropStart     = str(c_, "cropStart");
                c.cropEnd       = str(c_, "cropEnd");
                c.triggers      = c_.FindField("triggers")->strs;
            }
            for (const auto& mo : p_.Children("motions")) {
                const auto& m_ = obj(mo);
                MotionRecord& m = p.motions.emplace_back();
                m.animIndex     = str(m_, "animIndex");
                m.duration      = str(m_, "duration");
                m.translations  = m_.FindField("translations")->strs;
                m.rotations     = m_.FindField("rotations")->strs;
            }
        }
        return sf;
    }

    // ── whole-file emit (byte-identical: counts recomputed, content verbatim) ─────
    std::string EmitSingleFile(const SingleFile& sf)
    {
        std::string out;
        auto line = [&](std::string_view s) { out.append(s); out.append("\r\n"); };
        auto num  = [&](std::size_t n) { line(std::to_string(n)); };

        num(sf.projects.size());
        for (const auto& p : sf.projects) line(p.name);

        for (const auto& p : sf.projects) {
            std::size_t aLineCount = 3 + p.assetPaths.size();  // fieldX + assetCount + fieldY + K
            if (p.hasAnimData)
                for (const auto& c : p.clips) aLineCount += 7 + c.triggers.size();
            num(aLineCount);
            line(p.fieldX);
            num(p.assetPaths.size());
            for (const auto& a : p.assetPaths) line(a);
            line(p.hasAnimData ? "1" : "0");

            if (p.hasAnimData) {
                for (const auto& c : p.clips) {
                    line(c.name);
                    line(c.animIndex);
                    line(c.playbackSpeed);
                    line(c.cropStart);
                    line(c.cropEnd);
                    num(c.triggers.size());
                    for (const auto& t : c.triggers) line(t);
                    line("");  // separator
                }
                std::size_t bLineCount = 0;
                for (const auto& m : p.motions) bLineCount += 5 + m.translations.size() + m.rotations.size();
                num(bLineCount);
                for (const auto& m : p.motions) {
                    line(m.animIndex);
                    line(m.duration);
                    num(m.translations.size());
                    for (const auto& t : m.translations) line(t);
                    num(m.rotations.size());
                    for (const auto& r : m.rotations) line(r);
                    line("");  // separator
                }
            }
        }
        return out;
    }

}  // namespace CB::engine::animdata
