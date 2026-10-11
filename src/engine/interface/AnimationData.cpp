#include "interface/AnimationData.h"

#include <text/Text.h>

#include <charconv>

namespace CB::engine::animdata {

    namespace {

        // Cursor over a line vector with checked reads.
        struct Cursor {
            const std::vector<std::string_view>& lines;
            std::size_t                          p = 0;
            std::string_view next(const char* what)
            {
                if (p >= lines.size()) throw ParseError(std::string("unexpected EOF reading ") + what);
                return lines[p++];
            }
            bool done() const { return p >= lines.size(); }
        };

        std::size_t AsCount(std::string_view s, const char* what)
        {
            long long v = 0;
            const char* b = s.data();
            const char* e = b + s.size();
            auto [ptr, ec] = std::from_chars(b, e, v);
            if (ec != std::errc{} || ptr != e || v < 0)
                throw ParseError(std::string("expected a count for ") + what + ", got '" + std::string(s) + "'");
            return static_cast<std::size_t>(v);
        }

    }  // namespace

    // ── whole-file parse ─────────────────────────────────────────────────────────
    SingleFile ParseSingleFile(std::string_view text)
    {
        const std::vector<std::string_view> lines = CB::engine::text::SplitLines(text);
        Cursor                              cur{ lines };

        SingleFile sf;
        const std::size_t N = AsCount(cur.next("project count"), "project count");

        std::vector<std::string> names;
        names.reserve(N);
        for (std::size_t i = 0; i < N; ++i) names.emplace_back(cur.next("project name"));

        sf.projects.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            Project proj;
            proj.name = names[i];

            const std::size_t aLineCount = AsCount(cur.next("aLineCount"), "aLineCount");
            const std::size_t aStart     = cur.p;
            proj.fieldX = cur.next("fieldX");
            const std::size_t K = AsCount(cur.next("assetCount"), "assetCount");
            for (std::size_t k = 0; k < K; ++k) proj.assetPaths.emplace_back(cur.next("assetPath"));
            proj.hasAnimData = (cur.next("hasAnimData") == "1");

            if (proj.hasAnimData) {
                while (cur.p - aStart < aLineCount) {
                    ClipGenerator c;
                    c.name          = cur.next("clip name");
                    c.animIndex     = cur.next("clip animIndex");
                    c.playbackSpeed = cur.next("clip playbackSpeed");
                    c.cropStart     = cur.next("clip cropStart");
                    c.cropEnd       = cur.next("clip cropEnd");
                    const std::size_t T = AsCount(cur.next("triggerCount"), "triggerCount");
                    for (std::size_t t = 0; t < T; ++t) c.triggers.emplace_back(cur.next("trigger"));
                    cur.next("clip separator");  // blank line, counted
                    proj.clips.push_back(std::move(c));
                }
            }
            if (cur.p - aStart != aLineCount)
                throw ParseError("section A line-count mismatch for project '" + proj.name + "'");

            if (proj.hasAnimData) {
                const std::size_t bLineCount = AsCount(cur.next("bLineCount"), "bLineCount");
                const std::size_t bStart     = cur.p;
                while (cur.p - bStart < bLineCount) {
                    MotionRecord m;
                    m.animIndex = cur.next("motion animIndex");
                    m.duration  = cur.next("motion duration");
                    const std::size_t TC = AsCount(cur.next("translationCount"), "translationCount");
                    for (std::size_t t = 0; t < TC; ++t) m.translations.emplace_back(cur.next("translationKey"));
                    const std::size_t RC = AsCount(cur.next("rotationCount"), "rotationCount");
                    for (std::size_t r = 0; r < RC; ++r) m.rotations.emplace_back(cur.next("rotationKey"));
                    cur.next("motion separator");  // blank line, counted
                    proj.motions.push_back(std::move(m));
                }
                if (cur.p - bStart != bLineCount)
                    throw ParseError("section B line-count mismatch for project '" + proj.name + "'");
            }

            sf.projects.push_back(std::move(proj));
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
