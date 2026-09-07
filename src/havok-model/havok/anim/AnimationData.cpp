#include "havok/anim/AnimationData.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace havok::animdata {

    namespace {

        // Split into lines, stripping a trailing '\r' from each (tolerates LF or CRLF input).
        // When dropTrailingArtifact, a single final empty line (the artifact of a trailing
        // newline) is removed — internal blank separators are preserved. Whole-file parsing
        // wants that drop; per-file patch parsing does not care (it stops at the first blank).
        std::vector<std::string> SplitLines(std::string_view text, bool dropTrailingArtifact)
        {
            std::vector<std::string> lines;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= text.size(); ++i) {
                if (i == text.size() || text[i] == '\n') {
                    std::string_view raw = text.substr(start, i - start);
                    if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
                    lines.emplace_back(raw);
                    start = i + 1;
                    if (i == text.size()) break;
                }
            }
            // A "\n"-terminated file yields a trailing "" element; drop exactly that one.
            if (dropTrailingArtifact && !lines.empty() && lines.back().empty())
                lines.pop_back();
            return lines;
        }

        std::string ToLower(std::string s)
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
            return s;
        }

        // Cursor over a line vector with checked reads.
        struct Cursor {
            const std::vector<std::string>& lines;
            std::size_t                     p = 0;
            const std::string& next(const char* what)
            {
                if (p >= lines.size()) throw ParseError(std::string("unexpected EOF reading ") + what);
                return lines[p++];
            }
            bool done() const { return p >= lines.size(); }
        };

        std::size_t AsCount(const std::string& s, const char* what)
        {
            long long v = 0;
            const char* b = s.data();
            const char* e = b + s.size();
            auto [ptr, ec] = std::from_chars(b, e, v);
            if (ec != std::errc{} || ptr != e || v < 0)
                throw ParseError(std::string("expected a count for ") + what + ", got '" + s + "'");
            return static_cast<std::size_t>(v);
        }

        // Largest numeric animIndex across a project's clips + motions (0 if none numeric).
        long long MaxIndex(const Project& p)
        {
            long long mx = -1;
            auto scan = [&](const std::string& s) {
                long long v = 0;
                auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
                if (ec == std::errc{} && ptr == s.data() + s.size() && v > mx) mx = v;
            };
            for (const auto& c : p.clips)   scan(c.animIndex);
            for (const auto& m : p.motions) scan(m.animIndex);
            return mx;
        }

    }  // namespace

    // ── whole-file parse ─────────────────────────────────────────────────────────
    SingleFile ParseSingleFile(std::string_view text)
    {
        const std::vector<std::string> lines = SplitLines(text, /*dropTrailingArtifact*/ true);
        Cursor                         cur{ lines };

        SingleFile sf;
        const std::size_t N = AsCount(cur.next("project count"), "project count");

        std::vector<std::string> names;
        names.reserve(N);
        for (std::size_t i = 0; i < N; ++i) names.push_back(cur.next("project name"));

        sf.projects.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            Project proj;
            proj.name = names[i];

            const std::size_t aLineCount = AsCount(cur.next("aLineCount"), "aLineCount");
            const std::size_t aStart     = cur.p;
            proj.fieldX = cur.next("fieldX");
            const std::size_t K = AsCount(cur.next("assetCount"), "assetCount");
            for (std::size_t k = 0; k < K; ++k) proj.assetPaths.push_back(cur.next("assetPath"));
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
                    for (std::size_t t = 0; t < T; ++t) c.triggers.push_back(cur.next("trigger"));
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
                    for (std::size_t t = 0; t < TC; ++t) m.translations.push_back(cur.next("translationKey"));
                    const std::size_t RC = AsCount(cur.next("rotationCount"), "rotationCount");
                    for (std::size_t r = 0; r < RC; ++r) m.rotations.push_back(cur.next("rotationKey"));
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

    // ── Per-project (dev) form emitters ──────────────────────────────────────────
    // Byte-identical to the collated per-project blocks minus the line-count prefixes, so the
    // engine's per-project reader (which reads each file whole) accepts them as-is.
    std::string EmitDirList(const SingleFile& sf)
    {
        std::string out;
        for (const auto& p : sf.projects) { out.append(p.name); out.append("\r\n"); }
        return out;
    }

    std::string EmitProjectClips(const Project& p)
    {
        std::string out;
        auto line = [&](std::string_view s) { out.append(s); out.append("\r\n"); };
        auto num  = [&](std::size_t n) { line(std::to_string(n)); };

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
        }
        return out;
    }

    std::string EmitProjectMotion(const Project& p)
    {
        std::string out;
        auto line = [&](std::string_view s) { out.append(s); out.append("\r\n"); };
        auto num  = [&](std::size_t n) { line(std::to_string(n)); };
        for (const auto& m : p.motions) {
            line(m.animIndex);
            line(m.duration);
            num(m.translations.size());
            for (const auto& t : m.translations) line(t);
            num(m.rotations.size());
            for (const auto& r : m.rotations) line(r);
            line("");  // separator
        }
        return out;
    }

    // ── Nemesis patch parse ──────────────────────────────────────────────────────
    ClipGenerator ParsePatchClip(std::string_view text, std::string& outSymbol)
    {
        const std::vector<std::string> lines = SplitLines(text, /*dropTrailingArtifact*/ false);
        Cursor                         cur{ lines };
        ClipGenerator                  c;
        c.name          = cur.next("patch clip name");
        c.animIndex     = cur.next("patch clip symbol");
        outSymbol       = c.animIndex;
        c.playbackSpeed = cur.next("patch clip playbackSpeed");
        c.cropStart     = cur.next("patch clip cropStart");
        c.cropEnd       = cur.next("patch clip cropEnd");
        cur.next("patch clip triggerCount");  // declared count is unreliable — ignore it
        // Real trigger lines run until the first blank line or EOF.
        while (!cur.done() && !cur.lines[cur.p].empty()) c.triggers.push_back(cur.lines[cur.p++]);
        return c;
    }

    MotionRecord ParsePatchMotion(std::string_view text, std::string& outSymbol)
    {
        const std::vector<std::string> lines = SplitLines(text, /*dropTrailingArtifact*/ false);
        Cursor                         cur{ lines };
        MotionRecord                   m;
        m.animIndex = cur.next("patch motion symbol");
        outSymbol   = m.animIndex;
        m.duration  = cur.next("patch motion duration");
        const std::size_t TC = AsCount(cur.next("patch translationCount"), "patch translationCount");
        for (std::size_t t = 0; t < TC; ++t) m.translations.push_back(cur.next("patch translationKey"));
        const std::size_t RC = AsCount(cur.next("patch rotationCount"), "patch rotationCount");
        for (std::size_t r = 0; r < RC; ++r) m.rotations.push_back(cur.next("patch rotationKey"));
        return m;
    }

    ProjectPatch AssembleProjectPatch(std::string_view                                        projectDirName,
                                      const std::vector<std::pair<std::string, std::string>>& files)
    {
        ProjectPatch pp;
        const std::size_t tilde = projectDirName.find('~');
        pp.projectName = std::string(tilde == std::string_view::npos ? projectDirName
                                                                     : projectDirName.substr(0, tilde));

        std::vector<std::string> order;  // symbol order = first-seen file order
        std::vector<PatchAddition> adds;
        auto slot = [&](const std::string& sym) -> PatchAddition& {
            for (auto& a : adds)
                if (a.symbol == sym) return a;
            order.push_back(sym);
            adds.push_back(PatchAddition{});
            adds.back().symbol = sym;
            return adds.back();
        };

        for (const auto& [fname, content] : files) {
            std::string stem = fname;
            if (stem.size() >= 4 && ToLower(stem.substr(stem.size() - 4)) == ".txt")
                stem.resize(stem.size() - 4);
            std::string sym;
            if (stem.find('~') != std::string::npos) {
                ClipGenerator c = ParsePatchClip(content, sym);
                PatchAddition& a = slot(sym);
                a.clip    = std::move(c);
                a.hasClip = true;
            } else {
                MotionRecord m = ParsePatchMotion(content, sym);
                PatchAddition& a = slot(sym);
                a.motion    = std::move(m);
                a.hasMotion = true;
            }
        }
        pp.additions = std::move(adds);
        return pp;
    }

    // ── merge ────────────────────────────────────────────────────────────────────
    long long GlobalMaxIndex(const SingleFile& base)
    {
        long long mx = -1;
        for (const auto& p : base.projects) mx = std::max(mx, MaxIndex(p));
        return mx;
    }

    bool MergeProjectPatch(SingleFile& base, const ProjectPatch& patch, MergeStats& stats,
                           long long& a_nextIndex)
    {
        // Match by project name, case-insensitive, tolerating a ".txt" on either side.
        auto strip = [](std::string s) {
            const std::string ext = ".txt";
            if (s.size() >= ext.size() && ToLower(s.substr(s.size() - ext.size())) == ext)
                s.resize(s.size() - ext.size());
            return ToLower(std::move(s));
        };
        const std::string want = strip(patch.projectName);

        Project* proj = nullptr;
        for (auto& p : base.projects)
            if (strip(p.name) == want) { proj = &p; break; }
        if (!proj) return false;

        bool any = false;
        for (const auto& add : patch.additions) {
            const std::string idx = std::to_string(a_nextIndex++);
            if (add.hasClip) {
                ClipGenerator c = add.clip;
                c.animIndex = idx;
                proj->clips.push_back(std::move(c));
                ++stats.clipsAdded;
                any = true;
            }
            if (add.hasMotion) {
                MotionRecord m = add.motion;
                m.animIndex = idx;
                proj->motions.push_back(std::move(m));
                ++stats.motionsAdded;
                any = true;
            }
        }
        if (any && !proj->hasAnimData) proj->hasAnimData = true;  // header-only -> gains a section B
        if (any) ++stats.projectsPatched;
        return any;
    }

}  // namespace havok::animdata
