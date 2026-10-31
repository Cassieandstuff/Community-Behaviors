#include "codec/format/AnimationSetData.h"

#include <codec/resource/PathKey.h>   // path identity (project headers / set names are file names)
#include <codec/serialization/MetaText.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <tuple>
#include <utility>

// Pure std implementation — no SKSE/CommonLib/PCH — so it links into both the plugin and
// a standalone round-trip test. The format is positional (see AnimationSetData.h); every
// count is implicit, so each parse step is guarded and errors carry a 1-based line number.
namespace CB::engine::animsetdata {

    namespace {

        // Splits input into lines up front (stripping a trailing CR from each), then hands
        // them out via a cursor. Input may be LF or CRLF; a final trailing newline does not
        // produce a spurious empty line (so parse consumes exactly what emit produces).
        class LineReader {
        public:
            explicit LineReader(std::string_view text)
            {
                const std::size_t n = text.size();
                std::size_t start = 0;
                while (start <= n) {
                    const std::size_t nl = text.find('\n', start);
                    if (nl == std::string_view::npos) {
                        if (start < n) m_lines.push_back(text.substr(start));  // last line, no newline
                        break;
                    }
                    std::size_t end = nl;
                    if (end > start && text[end - 1] == '\r') --end;  // strip CR
                    m_lines.push_back(text.substr(start, end - start));
                    start = nl + 1;
                }
            }

            // 1-based number of the most recently consumed line (for error messages).
            std::size_t lineNo() const { return m_pos; }

            std::string_view next()
            {
                if (m_pos >= m_lines.size())
                    throw ParseError("unexpected end of file after line " + std::to_string(m_pos));
                return m_lines[m_pos++];
            }

            std::int64_t nextInt()
            {
                const std::string_view s = next();
                std::int64_t v = 0;
                const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
                if (res.ec != std::errc{} || res.ptr == s.data())
                    throw ParseError("expected integer at line " + std::to_string(m_pos) +
                                     " (got \"" + std::string(s) + "\")");
                return v;
            }

            std::uint32_t nextU32()
            {
                const std::string_view s = next();
                std::uint32_t v = 0;
                const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
                if (res.ec != std::errc{} || res.ptr == s.data())
                    throw ParseError("expected uint32 at line " + std::to_string(m_pos) +
                                     " (got \"" + std::string(s) + "\")");
                return v;
            }

            // A non-negative element count. Capped so a misread line can't spin a huge
            // reserve/loop — the largest vanilla section is ~150.
            std::size_t nextCount()
            {
                const std::int64_t v = nextInt();
                if (v < 0 || v > 5'000'000)
                    throw ParseError("implausible count " + std::to_string(v) + " at line " +
                                     std::to_string(m_pos));
                return static_cast<std::size_t>(v);
            }

        private:
            std::vector<std::string_view> m_lines;
            std::size_t                   m_pos = 0;
        };

        // Accumulates CRLF-terminated lines (including a terminator after the final line,
        // matching how the game ships the file).
        class LineWriter {
        public:
            void line(std::string_view s)
            {
                m_out.append(s);
                m_out.append("\r\n");
            }
            void count(std::size_t n) { line(std::to_string(n)); }
            std::string take() { return std::move(m_out); }

        private:
            std::string m_out;
        };

        // Parse one "V3" set-content record at the cursor (shared by single-file and
        // split-file paths). Does not touch SetFile::name.
        SetFile ParseSetContent(LineReader& r)
        {
            SetFile s;
            s.version = std::string(r.next());
            if (s.version != "V3")
                throw ParseError("unsupported animationsetdata version \"" + s.version +
                                 "\" at line " + std::to_string(r.lineNo()) + " (only V3 supported)");

            const std::size_t ne = r.nextCount();
            s.equipEvents.reserve(ne);
            for (std::size_t i = 0; i < ne; ++i) s.equipEvents.emplace_back(r.next());

            const std::size_t nc = r.nextCount();
            s.conditions.reserve(nc);
            for (std::size_t i = 0; i < nc; ++i) {
                TypeCondition c;
                c.variable = std::string(r.next());
                c.value = static_cast<std::int32_t>(r.nextInt());
                c.extra = static_cast<std::int32_t>(r.nextInt());
                s.conditions.push_back(std::move(c));
            }

            const std::size_t na = r.nextCount();
            s.attacks.reserve(na);
            for (std::size_t i = 0; i < na; ++i) {
                Attack a;
                a.event = std::string(r.next());
                a.flag = static_cast<std::int32_t>(r.nextInt());
                const std::size_t nk = r.nextCount();
                a.clips.reserve(nk);
                for (std::size_t k = 0; k < nk; ++k) a.clips.emplace_back(r.next());
                s.attacks.push_back(std::move(a));
            }

            const std::size_t nr = r.nextCount();
            s.crcs.reserve(nr);
            for (std::size_t i = 0; i < nr; ++i) {
                CrcTriple t;
                t.folder = r.nextU32();
                t.file = r.nextU32();
                t.ext = r.nextU32();
                s.crcs.push_back(t);
            }
            return s;
        }

        void EmitSetContent(LineWriter& w, const SetFile& s)
        {
            w.line(s.version);

            w.count(s.equipEvents.size());
            for (const auto& e : s.equipEvents) w.line(e);

            w.count(s.conditions.size());
            for (const auto& c : s.conditions) {
                w.line(c.variable);
                w.line(std::to_string(c.value));
                w.line(std::to_string(c.extra));
            }

            w.count(s.attacks.size());
            for (const auto& a : s.attacks) {
                w.line(a.event);
                w.line(std::to_string(a.flag));
                w.count(a.clips.size());
                for (const auto& c : a.clips) w.line(c);
            }

            w.count(s.crcs.size());
            for (const auto& t : s.crcs) {
                w.line(std::to_string(t.folder));
                w.line(std::to_string(t.file));
                w.line(std::to_string(t.ext));
            }
        }

    }  // namespace

    namespace {

        using codec::io::SchemaObject;

        const SchemaObject& Obj(const auto& o)
        {
            return static_cast<const SchemaObject&>(*o);
        }

        const std::string& Str(const SchemaObject& o, std::string_view f) { return o.FindField(f)->str; }

        template <class T>
        T Num(const SchemaObject& o, std::string_view f)
        {
            const std::string& s = Str(o, f);
            T v{};
            const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
            if (res.ec != std::errc{} || res.ptr != s.data() + s.size())
                throw ParseError(std::string(o.ClassName()) + "." + std::string(f) + ": expected a number, got \"" + s + "\"");
            return v;
        }

    }  // namespace

    SingleFile ReadSingleFile(const schema::SchemaRegistry& reg, std::string_view text)
    {
        std::string err;
        const auto  root = codec::io::ReadMetaText(reg, "AnimationSetDataSingleFile", text, err);
        if (!root) throw ParseError(err);
        SingleFile sf;
        for (const auto& po : root->Children("projects")) {
            const auto& po_ = Obj(po);
            Project&    p   = sf.projects.emplace_back();
            p.header        = Str(po_, "header");
            for (const auto& so : po_.Children("sets")) {
                const auto& s_ = Obj(so);
                SetFile&    s  = p.sets.emplace_back();
                s.name         = Str(s_, "name");
                s.version      = Str(s_, "version");
                if (s.version != "V3")
                    throw ParseError("unsupported animationsetdata version \"" + s.version + "\" in " + p.header + " / " + s.name);
                s.equipEvents = s_.FindField("equipEvents")->strs;
                for (const auto& co : s_.Children("conditions"))
                    s.conditions.push_back({ Str(Obj(co), "variable"), Num<std::int32_t>(Obj(co), "value"),
                                             Num<std::int32_t>(Obj(co), "extra") });
                for (const auto& ao : s_.Children("attacks"))
                    s.attacks.push_back({ Str(Obj(ao), "event"), Num<std::int32_t>(Obj(ao), "flag"), Obj(ao).FindField("clips")->strs });
                for (const auto& to : s_.Children("crcs")) {
                    CrcTriple t;
                    t.folder = Num<std::uint32_t>(Obj(to), "folder");
                    t.file   = Num<std::uint32_t>(Obj(to), "file");
                    t.ext    = Num<std::uint32_t>(Obj(to), "ext");
                    s.crcs.push_back(t);
                }
            }
        }
        return sf;
    }

    std::string EmitSingleFile(const SingleFile& sf)
    {
        LineWriter w;
        w.count(sf.projects.size());
        for (const auto& p : sf.projects) w.line(p.header);
        for (const auto& p : sf.projects) {
            w.count(p.sets.size());
            for (const auto& s : p.sets) w.line(s.name);
            for (const auto& s : p.sets) EmitSetContent(w, s);
        }
        return w.take();
    }

    SetFile ParseSetFile(std::string_view text)
    {
        LineReader r(text);
        return ParseSetContent(r);
    }

    // ── Nemesis patch-form ingest ────────────────────────────────────────────────

    namespace {

        // A marker-aware line walker for Nemesis patch files. Splits into lines (like
        // LineReader) but exposes markers so the section parser can flip mode on them.
        class NemesisReader {
        public:
            enum class Mode { Base, Insert, Original };

            explicit NemesisReader(std::string_view text)
            {
                const std::size_t n = text.size();
                std::size_t start = 0;
                while (start <= n) {
                    const std::size_t nl = text.find('\n', start);
                    if (nl == std::string_view::npos) {
                        if (start < n) m_lines.push_back(text.substr(start));
                        break;
                    }
                    std::size_t end = nl;
                    if (end > start && text[end - 1] == '\r') --end;
                    m_lines.push_back(text.substr(start, end - start));
                    start = nl + 1;
                }
            }

            Mode        mode() const { return m_mode; }
            std::size_t lineNo() const { return m_pos; }

            // Peek the next DATA line (consuming/handling any markers first), or false at EOF.
            bool peekData(std::string_view& out)
            {
                SkipMarkers();
                if (m_pos >= m_lines.size()) return false;
                out = m_lines[m_pos];
                return true;
            }

            // Consume one raw data line (must have been confirmed via peekData; asserts it
            // is not a marker so an item never straddles a marker).
            std::string_view nextData()
            {
                SkipMarkers();
                if (m_pos >= m_lines.size())
                    throw ParseError("Nemesis: unexpected end of file after line " + std::to_string(m_pos));
                const std::string_view l = m_lines[m_pos];
                if (IsMarker(l))
                    throw ParseError("Nemesis: marker inside an item at line " + std::to_string(m_pos + 1));
                ++m_pos;
                return l;
            }

            std::int64_t nextInt()
            {
                const std::string_view s = nextData();
                std::int64_t v = 0;
                const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
                if (res.ec != std::errc{} || res.ptr == s.data())
                    throw ParseError("Nemesis: expected integer at line " + std::to_string(m_pos) +
                                     " (got \"" + std::string(s) + "\")");
                return v;
            }

            std::uint32_t nextU32()
            {
                const std::string_view s = nextData();
                std::uint32_t v = 0;
                const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
                if (res.ec != std::errc{} || res.ptr == s.data())
                    throw ParseError("Nemesis: expected uint32 at line " + std::to_string(m_pos) +
                                     " (got \"" + std::string(s) + "\")");
                return v;
            }

        private:
            static bool IsMarker(std::string_view l)
            {
                const std::size_t b = l.find_first_not_of(" \t");
                return b != std::string_view::npos && l.compare(b, 4, "<!--") == 0;
            }

            // Consume consecutive marker lines, updating the mode for each.
            void SkipMarkers()
            {
                while (m_pos < m_lines.size() && IsMarker(m_lines[m_pos])) {
                    const std::string_view l = m_lines[m_pos++];
                    if (l.find("ORIGINAL") != std::string_view::npos) m_mode = Mode::Original;
                    else if (l.find("OPEN") != std::string_view::npos) m_mode = Mode::Insert;
                    else if (l.find("CLOSE") != std::string_view::npos) m_mode = Mode::Base;
                    // any other comment: leave mode unchanged
                }
            }

            std::vector<std::string_view> m_lines;
            std::size_t                   m_pos = 0;
            Mode                          m_mode = Mode::Base;
        };

    }  // namespace

    SetFile ParseNemesisSetFile(std::string_view text)
    {
        NemesisReader r(text);
        SetFile delta;

        // "V3"
        const std::string_view ver = r.nextData();
        if (ver != "V3")
            throw ParseError("Nemesis: unsupported version \"" + std::string(ver) + "\" (only V3)");
        delta.version = "V3";

        // Read a section: `count` BASE items, each read by readItem (which advances the
        // cursor correctly). Items encountered in Insert mode are the delta (via collect);
        // Base/Original items are consumed and discarded. Insertions may appear anywhere
        // (incl. trailing, after the last base item), so the loop keeps going past `count`
        // while the cursor sits in an Insert block.
        auto readSection = [&](std::size_t count, auto readItem) {
            std::size_t base = 0;
            std::string_view peek;
            while (r.peekData(peek)) {
                if (r.mode() == NemesisReader::Mode::Insert) {
                    readItem(/*collect=*/true);
                } else {
                    if (base >= count) break;  // a base-mode data line past the count = next section
                    readItem(/*collect=*/false);
                    ++base;
                }
            }
        };

        // §1 equip events (1 line each)
        readSection(static_cast<std::size_t>(r.nextInt()), [&](bool collect) {
            std::string_view e = r.nextData();
            if (collect) delta.equipEvents.emplace_back(e);
        });
        // §2 weapon-type conditions (name + 2 ints)
        readSection(static_cast<std::size_t>(r.nextInt()), [&](bool collect) {
            TypeCondition c;
            c.variable = std::string(r.nextData());
            c.value = static_cast<std::int32_t>(r.nextInt());
            c.extra = static_cast<std::int32_t>(r.nextInt());
            if (collect) delta.conditions.push_back(std::move(c));
        });
        // §3 attacks (event + flag + clipCount K + K clips)
        readSection(static_cast<std::size_t>(r.nextInt()), [&](bool collect) {
            Attack a;
            a.event = std::string(r.nextData());
            a.flag = static_cast<std::int32_t>(r.nextInt());
            const std::size_t nk = static_cast<std::size_t>(r.nextInt());
            for (std::size_t k = 0; k < nk; ++k) a.clips.emplace_back(r.nextData());
            if (collect) delta.attacks.push_back(std::move(a));
        });
        // §4 crc-triples (folder, file, ext)
        readSection(static_cast<std::size_t>(r.nextInt()), [&](bool collect) {
            CrcTriple t;
            t.folder = r.nextU32();
            t.file = r.nextU32();
            t.ext = r.nextU32();
            if (collect) delta.crcs.push_back(t);
        });

        return delta;
    }

    std::string EmitSetFile(const SetFile& set)
    {
        LineWriter w;
        EmitSetContent(w, set);
        return w.take();
    }

    // ── CRC / animation registration ─────────────────────────────────────────────
    // MOVED to CB::engine::codec::crc (src/engine/codec/crc). asd::TripleForAnimation is now a
    // using-alias (see AnimationSetData.h). Firesale step.

    // ── Merge ────────────────────────────────────────────────────────────────────

    namespace {

        /// @brief Project headers ("DefaultMaleData\DefaultMale.txt") and set names ("H2HDual.txt") are FILE names
        ///        (the asdsf packs the animationsetdata/ folder), so their identity is path identity.
        bool SameFile(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && codec::resource::PathKey(a) == codec::resource::PathKey(b);
        }

        void MergeSet(SetFile& base, const SetFile& delta, MergeStats& st)
        {
            /// @note Equip events, condition variables and attack events are Havok NAMES: matched exactly.
            for (const auto& e : delta.equipEvents) {
                const bool have = std::find(base.equipEvents.begin(), base.equipEvents.end(), e) != base.equipEvents.end();
                if (!have) { base.equipEvents.push_back(e); ++st.equipEventsAdded; }
            }

            // weapon-type conditions — append any whose variable the base lacks
            for (const auto& c : delta.conditions) {
                const bool have = std::any_of(base.conditions.begin(), base.conditions.end(),
                                              [&](const TypeCondition& x) { return x.variable == c.variable; });
                if (!have) { base.conditions.push_back(c); ++st.conditionsAdded; }
            }

            /// @note Attacks: delta wins on an event collision (its flag + clips; the base keeps its event string),
            ///       else append.
            for (const auto& a : delta.attacks) {
                auto it = std::find_if(base.attacks.begin(), base.attacks.end(),
                                       [&](const Attack& x) { return x.event == a.event; });
                if (it != base.attacks.end()) {
                    if (it->flag != a.flag || it->clips != a.clips) {
                        it->flag  = a.flag;
                        it->clips = a.clips;
                        ++st.attacksAdded;
                    }
                } else {
                    base.attacks.push_back(a);
                    ++st.attacksAdded;
                }
            }

            // crc-triples — append any exact (folder,file,ext) the base lacks
            std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> have;
            for (const auto& t : base.crcs) have.emplace(t.folder, t.file, t.ext);
            for (const auto& t : delta.crcs)
                if (have.emplace(t.folder, t.file, t.ext).second) {
                    base.crcs.push_back(t);
                    ++st.crcsAdded;
                }
        }

        void MergeProject(Project& base, const Project& delta, MergeStats& st)
        {
            for (const auto& ds : delta.sets) {
                auto it = std::find_if(base.sets.begin(), base.sets.end(),
                                       [&](const SetFile& x) { return SameFile(x.name, ds.name); });
                if (it != base.sets.end()) {
                    MergeSet(*it, ds, st);
                } else {
                    base.sets.push_back(ds);
                    ++st.setsAdded;
                }
            }
        }

    }  // namespace

    MergeStats MergeInto(SingleFile& base, const SingleFile& delta)
    {
        MergeStats st;
        for (const auto& dp : delta.projects) {
            auto it = std::find_if(base.projects.begin(), base.projects.end(),
                                   [&](const Project& x) { return SameFile(x.header, dp.header); });
            if (it != base.projects.end()) {
                MergeProject(*it, dp, st);
            } else {
                base.projects.push_back(dp);
                ++st.projectsAdded;
            }
        }
        return st;
    }

}  // namespace CB::engine::animsetdata
