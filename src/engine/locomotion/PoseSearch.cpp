#include "PoseSearch.h"

#include <codec/serialization/HavokIo.h>
#include <decompile/BehaviorDecompile.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <map>

namespace CB::engine::locomotion {

    namespace {

        namespace F = anim::features;
        using codec::io::SchemaObject;

        void Warn(const WarnFn& warn, const std::string& message)
        {
            if (warn) warn(message);
        }

        /// @return the curve named `name` (byte-exact) in `curves`, or null
        const anim::AnimCurveDef* CurveNamed(const std::vector<anim::AnimCurveDef>& curves, std::string_view name)
        {
            const auto it = std::find_if(curves.begin(), curves.end(), [&](const anim::AnimCurveDef& c) { return c.name == name; });
            return it == curves.end() ? nullptr : &*it;
        }

        /// @return string field `name` of `o`, "" when it holds none (an empty string may be left out of a unit).
        std::string StrOr(const SchemaObject& o, std::string_view name)
        {
            const auto* f = o.FindField(name);
            return f ? f->str : std::string();
        }

        /// @brief The normalized query vector and the playing row.
        struct Prepared {
            std::array<float, F::kDims> qv{};
            int                         curRow = -1;
        };

        /// @note The query: the trajectory ahead and back from the caller, the pose from the frame playing now.
        Prepared Prepare(const Database& db, const Query& q)
        {
            Prepared p;
            std::array<float, F::kDims>& qv = p.qv;
            for (int k = 0; k < 3; ++k) {
                const float raw[8] = { q.x[k], q.y[k], q.dirX[k], q.dirY[k], q.pastX[k], q.pastY[k], q.pastDirX[k], q.pastDirY[k] };
                const int   at[8]  = { F::kTrajPos + 2 * k, F::kTrajPos + 2 * k + 1, F::kTrajDir + 2 * k, F::kTrajDir + 2 * k + 1,
                                       F::kPastPos + 2 * k, F::kPastPos + 2 * k + 1, F::kPastDir + 2 * k, F::kPastDir + 2 * k + 1 };
                for (int j = 0; j < 8; ++j) qv[at[j]] = (raw[j] - db.mean[at[j]]) / db.scale[at[j]];
            }
            if (q.currentClip >= 0 && db.clips[q.currentClip].frames > 0) {
                const Clip& c = db.clips[q.currentClip];
                p.curRow      = c.first + FrameOf(c, q.currentTime);
                std::copy_n(db.rows.data() + static_cast<std::size_t>(p.curRow) * F::kDims + F::kFootPos, F::kDims - F::kFootPos,
                            qv.data() + F::kFootPos);
            }
            return p;
        }

        /// @brief The latest biases for `database` (all 0 outside `q.biases` or outside every database).
        Biases BiasesOf(const Query& q, int database)
        {
            return database >= 0 && static_cast<std::size_t>(database) < q.biases.size() ? q.biases[database] : Biases{};
        }

        /// @brief Unreal's additive biases for any candidate of `clip`: its database's base, plus looping for a looping child.
        float BiasOf(const Database& db, const Query& q, int clip)
        {
            const Clip&  c = db.clips[clip];
            const Biases b = BiasesOf(q, c.database);
            return b.base + ((c.flags & kFlagLooping) ? b.looping : 0.0f);
        }

        /// @brief Calls `visit(clip, row, bias)` for every row past the search's gates and bans, in row order.
        template <class Visit>
        void ForEachCandidate(const Database& db, const Query& q, int curRow, Visit&& visit)
        {
            std::size_t ban = 0;
            /// @note disableReselection: while such a child plays, no other frame of it is a candidate (it goes on or leaves).
            const bool noRepick = curRow >= 0 && (db.clips[q.currentClip].flags & kFlagDisableReselection);
            /// @note The candidate gates: standing (the request near zero) cuts only into rows its database's
            ///       standingCandidate passed, moving only into rows its movingCandidate passed (both evaluated at build).
            ///       Only the chooser's databases are searched; rows ascend with the children, so the ban walk stays monotonic.
            const bool standing = std::hypot(q.desiredX, q.desiredY) < kForceMoving;
            for (int ci = 0; ci < static_cast<int>(db.clips.size()); ++ci) {
                const Clip& c = db.clips[ci];
                if (c.frames == 0 || !IsActive(q.active, c.database) || (noRepick && ci == q.currentClip)) continue;
                const float b = BiasOf(db, q, ci);
                for (int r = c.first; r < c.first + c.frames; ++r) {
                    if (!db.searchable[r]) continue;
                    if (!(db.rowGates[r] & (standing ? kRowStanding : kRowMoving))) continue;
                    if (curRow >= 0 && ci == q.currentClip && std::abs(r - curRow) < kIgnoreSurrounding) continue;
                    while (ban < q.banned.size() && q.banned[ban].second < r) ++ban;
                    bool skip = false;
                    for (std::size_t k = ban; k < q.banned.size() && q.banned[k].first <= r; ++k)
                        if (q.banned[k].second >= r) { skip = true; break; }
                    if (skip) continue;
                    visit(ci, r, b);
                }
            }
        }

    }  // namespace

    int GroupOf(int d)
    {
        if (d < F::kTrajDir) return 0;
        if (d < F::kPastPos) return 1;
        if (d < F::kPastDir) return 2;
        if (d < F::kFootPos) return 3;
        if (d < F::kFootVel) return 4;
        if (d < F::kHipVel) return 5;
        return 6;
    }

    bool CurveVariables::Read(const char* name, relay::Value& out) const
    {
        const anim::AnimCurveDef* c = CurveNamed(m_curves, name);
        if (!c) return false;
        out = relay::Value::Float(anim::SampleCurve(*c, time));
        return true;
    }

    SchemaObject* RootSelector(const decompile::ParsedHky& g)
    {
        if (!g.root) return nullptr;
        for (const auto& nv : g.root->Children("namedVariants")) {
            const auto* v     = dynamic_cast<const SchemaObject*>(nv.get());
            auto*       graph = v ? v->Child("variant") : nullptr;
            if (!graph || std::string_view(graph->ClassName()) != "hkbBehaviorGraph") continue;
            auto* gen = graph->Child("rootGenerator");
            return gen && std::string_view(gen->ClassName()) == "CBPoseSearchGenerator" ? gen : nullptr;
        }
        return nullptr;
    }

    void AppendRowGaits(const anim::MmCachedClip& c, Measured& out)
    {
        const anim::AnimCurveDef* speed = CurveNamed(c.curves, kSpeedCurve);
        for (int i = 0; i < c.frames; i++) {
            const float s = speed ? anim::SampleCurve(*speed, static_cast<float>(i * out.dt)) : 0.0f;
            const Gait gait = s >= kRunSprint ? Gait::Sprint
                            : s >= kWalkRun   ? Gait::Run
                            : s >= kStandSpeed ? Gait::Walk
                                               : Gait::None;
            out.gait.push_back(static_cast<std::uint8_t>(gait));
        }
    }

    void CollectGaitSpeeds(const anim::MmCachedClip& c, double dt, const std::vector<std::uint8_t>& gait,
                           std::array<std::vector<float>, 4>& out)
    {
        const anim::AnimCurveDef* direction = CurveNamed(c.curves, kDirectionCurve);
        const anim::AnimCurveDef* accel     = CurveNamed(c.curves, kAccelCurve);
        if (!direction || !accel || gait.size() < static_cast<std::size_t>(c.frames)) return;
        const auto& p = c.rootPositions;
        for (int i = 0; i + 1 < c.frames; ++i) {
            const float t = static_cast<float>(i * dt);
            if (gait[i] == static_cast<std::uint8_t>(Gait::None) || std::fabs(anim::SampleCurve(*direction, t)) >= kForwardDeg ||
                std::fabs(anim::SampleCurve(*accel, t)) >= kSteadyAccel)
                continue;
            const double dx = p[(i + 1) * 3] - p[i * 3], dy = p[(i + 1) * 3 + 1] - p[i * 3 + 1];
            out[gait[i]].push_back(static_cast<float>(std::sqrt(dx * dx + dy * dy) / dt));
        }
    }

    std::unique_ptr<Measured> MeasuredFrom(const anim::MmCachedClip& c, std::array<std::vector<float>, 4>& gaitSpeeds)
    {
        auto out          = std::make_unique<Measured>();
        out->rows         = c.features;
        out->frames       = c.frames;
        out->dt           = c.frameDuration;
        out->duration     = c.duration;
        const int horizon = static_cast<int>(std::lround(F::kHorizons.back() / out->dt));
        for (int i = 0; i < c.frames; ++i) out->searchable.push_back(i + horizon <= c.frames - 1);
        AppendRowGaits(c, *out);
        CollectGaitSpeeds(c, out->dt, out->gait, gaitSpeeds);
        out->curves = c.curves;
        return out;
    }

    Expression CompileExpression(std::string text, std::string_view what, std::string_view fallback, const WarnFn& warn)
    {
        Expression e{ std::move(text), {} };
        if (e.text.empty()) return e;
        std::string err;
        e.expr = relay::Compile(e.text, err);
        if (!e.expr.Valid()) Warn(warn, std::format("{} '{}' does not compile ({}) - {}", what, e.text, err, fallback));
        return e;
    }

    void ReadDatabases(const SchemaObject& sel, Database& db, const WarnFn& warn)
    {
        const auto* field   = sel.HasField("databases") ? sel.FindField("databases") : nullptr;
        const bool  implied = !field || field->objs.empty();
        if (implied) {
            db.databases.push_back({ "(every child)" });
        } else {
            for (const auto& o : field->objs) {
                const auto* d = dynamic_cast<const SchemaObject*>(o.get());
                if (!d) continue;
                PoseDatabase p;
                p.name       = d->Str("name");
                p.base       = CompileExpression(StrOr(*d, "baseCostBias"), "database '" + p.name + "' baseCostBias", "0", warn);
                p.continuing = CompileExpression(StrOr(*d, "continuingPoseCostBias"), "database '" + p.name + "' continuingPoseCostBias", "0", warn);
                p.looping    = CompileExpression(StrOr(*d, "loopingCostBias"), "database '" + p.name + "' loopingCostBias", "0", warn);
                p.standing   = CompileExpression(StrOr(*d, "standingCandidate"), "database '" + p.name + "' standingCandidate",
                                                 "no row is a standing candidate", warn);
                p.moving     = CompileExpression(StrOr(*d, "movingCandidate"), "database '" + p.name + "' movingCandidate",
                                                 "no row is a moving candidate", warn);
                if (const auto* tags = d->FindField("tags"))
                    p.pivots = std::find(tags->strs.begin(), tags->strs.end(), "Pivots") != tags->strs.end();
                db.databases.push_back(std::move(p));
            }
        }
        const auto& rows = sel.Children("children");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto* row = dynamic_cast<const SchemaObject*>(rows[i].get());
            if (!row) continue;
            Clip& c      = db.clips[i];
            c.flags      = static_cast<std::uint8_t>((row->GetBool("disableReselection") ? kFlagDisableReselection : 0) |
                                                     (row->GetBool("looping") ? kFlagLooping : 0));
            c.rangeStart = row->GetFloat("rangeStart");
            c.rangeEnd   = row->GetFloat("rangeEnd");
            if (implied) {
                c.database = 0;
                continue;
            }
            const std::string& name = row->Str("database");
            const auto         it   = std::find_if(db.databases.begin(), db.databases.end(), [&](const PoseDatabase& d) { return d.name == name; });
            if (it == db.databases.end()) Warn(warn, std::format("child {} names database '{}', which the graph lacks - never searched", i, name));
            else c.database = static_cast<int>(it - db.databases.begin());
        }
    }

    std::vector<int> DatabasesNamed(const Database& db, const std::vector<std::string>& names, std::string_view where, const WarnFn& warn)
    {
        std::vector<int> out;
        for (const auto& name : names) {
            const auto it = std::find_if(db.databases.begin(), db.databases.end(), [&](const PoseDatabase& d) { return d.name == name; });
            if (it == db.databases.end()) Warn(warn, std::format("the chooser's {} names database '{}', which the graph lacks - ignored", where, name));
            else out.push_back(static_cast<int>(it - db.databases.begin()));
        }
        return out;
    }

    void ReadChooser(const SchemaObject& sel, Database& db, const WarnFn& warn)
    {
        const SchemaObject* chooser = sel.HasField("chooser") ? sel.Child("chooser") : nullptr;
        if (!chooser) return;
        db.haveChooser = true;
        if (const auto* rows = chooser->FindField("rows"))
            for (const auto& o : rows->objs) {
                const auto* r = dynamic_cast<const SchemaObject*>(o.get());
                if (!r) continue;
                ChooserRow row;
                row.when = CompileExpression(StrOr(*r, "when"), "chooser row " + std::to_string(db.chooser.size()) + " `when`",
                                             "the row never holds", warn);
                if (const auto* names = r->FindField("databases")) row.databases = DatabasesNamed(db, names->strs, "row", warn);
                db.chooser.push_back(std::move(row));
            }
        if (const auto* fallback = chooser->FindField("fallback")) db.fallback = DatabasesNamed(db, fallback->strs, "fallback", warn);
    }

    std::unique_ptr<Database> BuildDatabase(const SchemaObject& selector, const ClipSource& clips, const WarnFn& warn, BuildReport* report)
    {
        auto        db       = std::make_unique<Database>();
        const auto& children = selector.Children("children");
        db->clips.resize(children.size());
        ReadDatabases(selector, *db, warn);
        ReadChooser(selector, *db, warn);

        /// @note Keyed by the exact animationName: case is never folded (a differently cased name is its own clip).
        std::map<std::string, std::unique_ptr<Measured>> measured;
        std::vector<const Measured*>                     of(children.size(), nullptr);
        std::array<std::vector<float>, 4>                gaitSpeeds;
        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto* row = dynamic_cast<const SchemaObject*>(children[i].get());
            const auto* gen = row ? row->Child("generator") : nullptr;
            if (!gen || std::string_view(gen->ClassName()) != "hkbClipGenerator") {
                Warn(warn, std::format("selector child {} is not a clip generator - never chosen", i));
                continue;
            }
            Clip& c = db->clips[i];
            c.name  = gen->Str("animationName");
            auto it = measured.find(c.name);
            if (it == measured.end()) {
                std::unique_ptr<Measured> m;
                try {
                    m = MeasuredFrom(clips(c.name), gaitSpeeds);
                } catch (const std::exception& e) {
                    Warn(warn, std::format("clip {} '{}' not measured ({}) - never chosen", i, c.name, e.what()));
                }
                it = measured.emplace(c.name, std::move(m)).first;
            }
            of[i] = it->second.get();
        }

        /// @note Holden's normalization: each dimension centred on its mean, then each GROUP divided by its mean
        ///       spread, so a group's dimensions keep their relative sizes and every group weighs in comparably.
        ///       Taken over the distinct clips, so a clip listed in several databases does not weigh more.
        std::array<double, F::kDims> mean{}, var{};
        std::size_t                  distinctRows = 0, measuredClips = 0;
        for (const auto& [name, m] : measured) {
            if (!m) continue;
            ++measuredClips;
            distinctRows += static_cast<std::size_t>(m->frames);
            for (std::size_t k = 0; k < m->rows.size(); ++k) mean[k % F::kDims] += m->rows[k];
        }
        if (distinctRows == 0) return nullptr;
        for (auto& m : mean) m /= static_cast<double>(distinctRows);
        for (const auto& [name, m] : measured) {
            if (!m) continue;
            for (std::size_t k = 0; k < m->rows.size(); ++k) {
                const double x = m->rows[k] - mean[k % F::kDims];
                var[k % F::kDims] += x * x;
            }
        }
        std::array<double, kGroups> groupSpread{}, groupDims{};
        for (int d = 0; d < F::kDims; ++d) {
            groupSpread[GroupOf(d)] += std::sqrt(var[d] / static_cast<double>(distinctRows));
            groupDims[GroupOf(d)] += 1;
        }
        const std::array<float, kGroups> weights{ kWeightTrajPos, kWeightTrajDir, kWeightPastPos, kWeightPastDir,
                                                  kWeightFootPos, kWeightFootVel, kWeightHipVel };
        for (int d = 0; d < F::kDims; ++d) {
            const double spread = groupSpread[GroupOf(d)] / groupDims[GroupOf(d)];
            db->mean[d]   = static_cast<float>(mean[d]);
            db->scale[d]  = static_cast<float>(spread > 1.0e-6 ? spread : 1.0);
            db->weight[d] = weights[GroupOf(d)];
        }

        std::size_t total = 0;
        for (const Measured* m : of) total += m ? static_cast<std::size_t>(m->frames) : 0;
        db->rows.reserve(total * F::kDims);
        db->clipOf.reserve(total);
        db->searchable.reserve(total);
        db->rowGates.reserve(total);
        db->rowGait.reserve(total);
        /// @note The gates read only the row's curves: the build has no actor, and the context is reset per row so
        ///       the primitive memo never hands one row's curve value to the next.
        relay::EvalContext ctx;
        const auto         gate = [&](const Expression& e) {
            if (e.text.empty()) return true;
            return e.expr.Valid() && e.expr.Evaluate(ctx).AsBool();
        };
        for (std::size_t i = 0; i < db->clips.size(); ++i) {
            Clip& c = db->clips[i];
            c.first = static_cast<int>(db->rows.size() / F::kDims);
            const Measured* m = of[i];
            if (!m) continue;
            c.frames   = m->frames;
            c.dt       = m->dt;
            c.duration = m->duration;
            for (std::size_t k = 0; k < m->rows.size(); ++k) {
                const int d = static_cast<int>(k % F::kDims);
                db->rows.push_back((m->rows[k] - db->mean[d]) / db->scale[d]);
            }
            db->clipOf.insert(db->clipOf.end(), static_cast<std::size_t>(m->frames), static_cast<int>(i));
            db->searchable.insert(db->searchable.end(), m->searchable.begin(), m->searchable.end());
            /// @note The range cuts per CHILD: children sharing a clip share its Measured, not its range.
            for (int f = 0; f < m->frames; ++f) {
                const double t = f * m->dt;
                if (t < c.rangeStart || (c.rangeEnd > 0 && t > c.rangeEnd)) db->searchable[c.first + f] = 0;
            }
            db->rowGait.insert(db->rowGait.end(), m->gait.begin(), m->gait.end());
            const PoseDatabase* pd = c.database >= 0 ? &db->databases[c.database] : nullptr;
            CurveVariables      vars(m->curves);
            for (int f = 0; f < m->frames; ++f) {
                std::uint8_t bits = 0;
                if (pd) {
                    ctx.Reset(nullptr);
                    ctx.variables = &vars;
                    vars.time     = static_cast<float>(f * m->dt);
                    if (gate(pd->standing)) bits |= kRowStanding;
                    if (gate(pd->moving)) bits |= kRowMoving;
                }
                db->rowGates.push_back(bits);
            }
        }

        for (std::size_t g = 0; g < gaitSpeeds.size(); ++g) {
            auto& v = gaitSpeeds[g];
            if (v.empty()) continue;
            std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
            db->gaitSpeed[g] = v[v.size() / 2];
        }

        if (report) {
            report->distinctClips = measured.size();
            report->measuredClips = measuredClips;
            report->frames        = total;
            for (std::size_t g = 0; g < gaitSpeeds.size(); ++g) report->gaitFrames[g] = gaitSpeeds[g].size();
        }
        return db;
    }

    int FrameOf(const Clip& c, double time)
    {
        return std::clamp(static_cast<int>(std::lround(time / c.dt)), 0, c.frames - 1);
    }

    float ValueOf(const Expression& e, const relay::EvalContext& ctx)
    {
        return e.expr.Valid() ? e.expr.Evaluate(ctx).AsFloat() : 0.0f;
    }

    std::vector<Biases> EvaluateBiases(const Database& db, const relay::EvalContext& ctx)
    {
        std::vector<Biases> out(db.databases.size());
        for (std::size_t d = 0; d < out.size(); ++d) {
            const PoseDatabase& p = db.databases[d];
            out[d] = { ValueOf(p.base, ctx), ValueOf(p.continuing, ctx), ValueOf(p.looping, ctx) };
        }
        return out;
    }

    std::vector<char> Choose(const Database& db, const relay::EvalContext& ctx)
    {
        std::vector<char> on(db.databases.size(), 0);
        bool              any = false;
        for (const ChooserRow& row : db.chooser) {
            const bool holds = row.when.text.empty() || (row.when.expr.Valid() && row.when.expr.Evaluate(ctx).AsBool());
            if (!holds) continue;
            for (const int d : row.databases) on[d] = 1, any = true;
        }
        if (any) return on;
        if (db.fallback.empty()) std::fill(on.begin(), on.end(), 1);
        for (const int d : db.fallback) on[d] = 1;
        return on;
    }

    bool IsActive(const std::vector<char>& active, int database)
    {
        return database >= 0 && (active.empty() || (static_cast<std::size_t>(database) < active.size() && active[database]));
    }

    SearchResult Search(const Database& db, const Query& q)
    {
        const Prepared p      = Prepare(db, q);
        const auto&    qv     = p.qv;
        const int      curRow = p.curRow;

        const auto cost = [&](std::size_t r, float bound) {
            const float* row = db.rows.data() + r * F::kDims;
            float        sum = 0;
            for (int d = 0; d < F::kDims && sum < bound; ++d) {
                const float e = db.weight[d] * (qv[d] - row[d]);
                sum += e * e;
            }
            return sum;
        };

        /// @note The playing row (the continuing pose) carries its database's continuing bias on top of a candidate's.
        const float playing = curRow >= 0 ? cost(static_cast<std::size_t>(curRow), 1.0e30f) + BiasOf(db, q, q.currentClip) +
                                                BiasesOf(q, db.clips[q.currentClip].database).continuing
                                          : 1.0e30f;
        float       best     = 1.0e30f;
        int         bestRow  = -1;
        std::size_t searched = 0;
        ForEachCandidate(db, q, curRow, [&](int, int r, float b) {
            ++searched;
            const float f = cost(static_cast<std::size_t>(r), best - b);
            if (f + b < best) best = f + b, bestRow = r;
        });

        SearchResult out;
        out.bestRow    = bestRow;
        out.best       = best;
        out.playing    = playing;
        out.playingRow = curRow;
        out.searched   = searched;
        /// @note At a clip's end or an interrupt the playing row cannot go on, so the best candidate wins outright;
        ///       otherwise its biased cost must be below the playing row's.
        out.wins       = bestRow >= 0 && (curRow < 0 || q.clipEnd || q.interrupt || best < playing);
        return out;
    }

    std::vector<Candidate> TopCandidates(const Database& db, const Query& q, std::size_t n)
    {
        std::vector<Candidate> out;
        if (n == 0) return out;
        const Prepared p = Prepare(db, q);
        const auto     better = [](const Candidate& a, const Candidate& b) { return a.total < b.total || (a.total == b.total && a.row < b.row); };
        ForEachCandidate(db, q, p.curRow, [&](int ci, int r, float b) {
            const float* row = db.rows.data() + static_cast<std::size_t>(r) * F::kDims;
            Candidate    c;
            float        sum = 0;
            for (int d = 0; d < F::kDims; ++d) {
                const float e = db.weight[d] * (p.qv[d] - row[d]);
                sum += e * e;
                c.groupCost[GroupOf(d)] += e * e;
            }
            c.row   = r;
            c.clip  = ci;
            c.frame = r - db.clips[ci].first;
            c.total = sum + b;
            c.bias  = b;
            /// @note The search's first bound: past it (or NaN) a row never wins there either.
            if (!(c.total < 1.0e30f)) return;
            if (out.size() < n) {
                out.push_back(c);
                std::push_heap(out.begin(), out.end(), better);
            } else if (better(c, out.front())) {
                std::pop_heap(out.begin(), out.end(), better);
                out.back() = c;
                std::push_heap(out.begin(), out.end(), better);
            }
        });
        std::sort_heap(out.begin(), out.end(), better);
        return out;
    }

}  // namespace CB::engine::locomotion
