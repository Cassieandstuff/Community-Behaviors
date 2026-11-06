#include "PoseSearch.h"

#include <codec/serialization/HavokIo.h>
#include <decompile/BehaviorDecompile.h>

#include <algorithm>
#include <bit>
#include <chrono>
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

        /// @brief The question in each space, normalized, made the first time a candidate needs it, and the playing row.
        /// @note  The trajectory comes from the caller (each dim from its nearest query sample), the pose from the frame
        ///        playing now, read in the same space.
        class Prepared {
        public:
            Prepared(const Database& db, const Query& q) : m_db(db), m_q(q), m_qv(db.spaces.size())
            {
                if (q.currentClip >= 0 && db.clips[q.currentClip].frames > 0) {
                    const Clip& c = db.clips[q.currentClip];
                    curRow        = c.first + FrameOf(c, q.currentTime);
                }
            }
            const std::vector<float>& In(int space)
            {
                std::vector<float>& qv = m_qv[space];
                if (!qv.empty()) return qv;
                const SchemaSpace& sp = m_db.spaces[space];
                qv.assign(static_cast<std::size_t>(sp.dims), 0.0f);
                for (int d = 0; d < sp.dims; ++d) {
                    if (sp.querySample[d] >= 0) {
                        const TrajectorySample& t = m_q.samples[sp.querySample[d]];
                        const float raw[F::kPerSample] = { t.x, t.y, t.dirX, t.dirY, t.vx, t.vy, t.z };
                        qv[d] = (raw[sp.queryPart[d]] - sp.mean[d]) / sp.scale[d];
                    } else if (curRow >= 0) {
                        qv[d] = sp.rows[static_cast<std::size_t>(curRow) * sp.dims + d];
                    }
                }
                return qv;
            }
            int curRow = -1;

        private:
            const Database&                 m_db;
            const Query&                    m_q;
            std::vector<std::vector<float>> m_qv;
        };

        /// @return the space a candidate of `clip` is weighed in
        int SpaceOf(const Database& db, int clip)
        {
            const int d = db.clips[clip].database;
            return d >= 0 ? db.databases[d].space : 0;
        }

        /// @brief Row `r`'s cost in `sp` against `qv`: the weighted sum of squared differences, cut short past `bound`.
        /// @note  Weight times the square, as Unreal's schemas mean their weights.
        float CostIn(const SchemaSpace& sp, const std::vector<float>& qv, std::size_t r, float bound)
        {
            if (sp.rows.empty()) return 1.0e30f;
            const float* row = sp.rows.data() + r * sp.dims;
            float        sum = 0;
            for (const int d : sp.active) {
                if (!(sum < bound)) break;
                const float e = qv[d] - row[d];
                sum += sp.weight[d] * e * e;
            }
            return sum;
        }

        /// @brief The classic layout's space: anim::AppendClipFeatures' rows, DefaultWeights, the trajectory from the query's
        ///        samples one to one.
        SchemaSpace ClassicSpace()
        {
            SchemaSpace sp;
            sp.classic = true;
            sp.dims    = F::kDims;
            const auto w = DefaultWeights();
            sp.weight.assign(w.begin(), w.end());
            for (int d = 0; d < F::kDims; ++d) {
                const bool path = d < F::kFootPos;
                sp.querySample.push_back(path ? d / F::kPerSample : -1);
                sp.queryPart.push_back(path ? d % F::kPerSample : 0);
                sp.group.push_back(GroupOf(d));
            }
            return sp;
        }

        /// @brief A typed schema's space: its weights, each trajectory sample read from the query sample nearest its time,
        ///        each dim's cost summed into its feature.
        SchemaSpace TypedSpace(const anim::SchemaSpec& spec, const anim::Rig& rig)
        {
            SchemaSpace sp;
            sp.classic = false;
            sp.spec    = spec;
            sp.key     = anim::SchemaKey(spec, rig);
            sp.dims    = anim::SchemaDims(spec);
            sp.weight  = anim::SchemaWeights(spec);
            for (std::size_t k = 0; k < spec.features.size(); ++k) {
                const anim::FeatureSpec& f = spec.features[k];
                if (f.kind == anim::FeatureKind::Trajectory) {
                    for (const auto& smp : f.samples) {
                        int nearest = 0;
                        for (int s = 1; s < F::kSamples; ++s)
                            if (std::fabs(F::SampleTime(s) - smp.time) < std::fabs(F::SampleTime(nearest) - smp.time)) nearest = s;
                        for (int j = 0; j < F::kPerSample; ++j) {
                            sp.querySample.push_back(nearest);
                            sp.queryPart.push_back(j);
                            sp.group.push_back(static_cast<int>(k));
                        }
                    }
                    continue;
                }
                for (int j = 0; j < anim::FeatureDims(f); ++j) {
                    sp.querySample.push_back(-1);
                    sp.queryPart.push_back(0);
                    sp.group.push_back(static_cast<int>(k));
                }
            }
            return sp;
        }

        /// @brief The normalization groups in `sp`: the classic layout's NormGroupOf; a typed schema's per trajectory
        ///        channel and direction within each trajectory feature, and per other feature.
        std::vector<int> NormGroupsOf(const SchemaSpace& sp);

        /// @brief The normalization groups: per direction (ahead, back) each channel, then each pose part.
        int NormGroupOf(int d)
        {
            if (d < F::kFootPos) {
                const int sample = d / F::kPerSample, within = d % F::kPerSample;
                const int channel = within < F::kSampleFacing ? 0 : within < F::kSampleVelocity ? 1 : within < F::kSampleHeight ? 2 : 3;
                return (sample < static_cast<int>(F::kAhead.size()) ? 0 : kChannels) + channel;
            }
            return 2 * kChannels + (WeightGroupOf(d) - F::kSamples * kChannels);
        }
        constexpr int kNormGroups = 2 * kChannels + 5;

        std::vector<int> NormGroupsOf(const SchemaSpace& sp)
        {
            std::vector<int> out(static_cast<std::size_t>(sp.dims), 0);
            if (sp.classic) {
                for (int d = 0; d < sp.dims; ++d) out[d] = NormGroupOf(d);
                return out;
            }
            int d = 0;
            for (std::size_t k = 0; k < sp.spec.features.size(); ++k) {
                const anim::FeatureSpec& f = sp.spec.features[k];
                const int                base = static_cast<int>(k) * 2 * kChannels;
                if (f.kind != anim::FeatureKind::Trajectory) {
                    for (int j = 0; j < anim::FeatureDims(f); ++j) out[d++] = base;
                    continue;
                }
                for (const auto& smp : f.samples)
                    for (int j = 0; j < F::kPerSample; ++j) {
                        const int channel = j < F::kSampleFacing ? 0 : j < F::kSampleVelocity ? 1 : j < F::kSampleHeight ? 2 : 3;
                        out[d++] = base + (smp.time < 0 ? kChannels : 0) + channel;
                    }
            }
            return out;
        }

        constexpr const char* kChannelNames[kChannels] = { "position", "facing", "velocity", "height" };
        constexpr const char* kPoseNames[5]            = { "feet", "feetVelocity", "hipVelocity", "footSpacing", "pelvisFacing" };

        /// @return `seconds` as a sample label: "+0.2", "+0.33", "-1"
        std::string SampleLabel(double seconds)
        {
            std::string out = std::format("{:.2f}", std::fabs(seconds));
            while (out.back() == '0') out.pop_back();
            if (out.back() == '.') out.pop_back();
            return (seconds < 0 ? "-" : "+") + out;
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

    int WeightGroupOf(int d)
    {
        if (d < F::kFootPos) {
            const int within  = d % F::kPerSample;
            const int channel = within < F::kSampleFacing ? 0 : within < F::kSampleVelocity ? 1 : within < F::kSampleHeight ? 2 : 3;
            return (d / F::kPerSample) * kChannels + channel;
        }
        const int pose = d < F::kFootVel ? 0 : d < F::kHipVel ? 1 : d < F::kFootSpacing ? 2 : d < F::kPelvisFacing ? 3 : 4;
        return F::kSamples * kChannels + pose;
    }

    std::string WeightGroupName(int g)
    {
        if (g < 0 || g >= kWeightGroups) return {};
        if (g >= F::kSamples * kChannels) return kPoseNames[g - F::kSamples * kChannels];
        return kChannelNames[g % kChannels] + SampleLabel(F::SampleTime(g / kChannels));
    }

    std::array<float, F::kDims> DefaultWeights()
    {
        std::array<float, F::kDims> out{};
        const auto classic = [](double seconds) {
            const double t = std::fabs(seconds);
            return std::fabs(t - 1.0 / 3.0) < 1.0e-6 || std::fabs(t - 2.0 / 3.0) < 1.0e-6 || std::fabs(t - 1.0) < 1.0e-6;
        };
        for (int d = 0; d < F::kDims; ++d) {
            const int g = WeightGroupOf(d);
            if (g < F::kSamples * kChannels) {
                if (!classic(F::SampleTime(g / kChannels))) continue;
                if (g % kChannels == 0) out[d] = kWeightTrajPos;
                if (g % kChannels == 1) out[d] = kWeightTrajDir;
                continue;
            }
            const int pose = g - F::kSamples * kChannels;
            out[d] = pose == 0 ? kWeightFootPos : pose == 1 ? kWeightFootVel : pose == 2 ? kWeightHipVel : 0.0f;
        }
        return out;
    }

    std::vector<std::string> ApplyWeights(const std::vector<std::pair<std::string, float>>& weights, std::array<float, F::kDims>& out)
    {
        /// @note Three passes, broadest first: a channel alone, a channel with a direction, then one group.
        std::vector<char> used(weights.size(), 0);
        for (int pass = 0; pass < 3; ++pass)
            for (std::size_t w = 0; w < weights.size(); ++w) {
                const std::string& name = weights[w].first;
                for (int d = 0; d < F::kDims; ++d) {
                    const int g = WeightGroupOf(d);
                    bool      hit = false;
                    if (g < F::kSamples * kChannels) {
                        const std::string channel = kChannelNames[g % kChannels];
                        const bool        ahead   = g / kChannels < static_cast<int>(F::kAhead.size());
                        if (pass == 0) hit = name == channel;
                        if (pass == 1) hit = name == channel + (ahead ? "+" : "-");
                    }
                    if (pass == 2) hit = name == WeightGroupName(g);
                    if (!hit) continue;
                    out[d]  = weights[w].second;
                    used[w] = 1;
                }
            }
        std::vector<std::string> unknown;
        for (std::size_t w = 0; w < weights.size(); ++w)
            if (!used[w]) unknown.push_back(weights[w].first);
        return unknown;
    }

    int GroupOf(int d)
    {
        const int g = WeightGroupOf(d);
        if (g < F::kSamples * kChannels) {
            const bool ahead   = g / kChannels < static_cast<int>(F::kAhead.size());
            const int  channel = g % kChannels;
            if (channel == 3) return 5;
            if (ahead) return channel;
            return channel == 1 ? 4 : 3;
        }
        const int pose = g - F::kSamples * kChannels;
        return pose == 0 || pose == 3 ? 6 : pose == 1 ? 7 : 8;
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

    std::vector<float> WindowedRootSpeeds(const std::vector<float>& p, int frames, double dt)
    {
        std::vector<float> step(static_cast<std::size_t>((std::max)(frames, 0)), 0.0f), out(step.size(), 0.0f);
        if (frames < 2 || dt <= 0.0 || p.size() < static_cast<std::size_t>(frames) * 3) return out;
        for (int i = 0; i + 1 < frames; ++i) {
            const double dx = p[(i + 1) * 3] - p[i * 3], dy = p[(i + 1) * 3 + 1] - p[i * 3 + 1];
            step[i] = static_cast<float>(std::sqrt(dx * dx + dy * dy) / dt);
        }
        step[frames - 1] = step[frames - 2];
        const int half = (std::max)(1, static_cast<int>(std::lround(kClipSpeedWindow / dt)));
        for (int i = 0; i < frames; ++i) {
            const int lo = (std::max)(0, i - half), hi = (std::min)(frames - 1, i + half);
            double    sum = 0;
            for (int k = lo; k <= hi; ++k) sum += step[k];
            out[i] = static_cast<float>(sum / (hi - lo + 1));
        }
        return out;
    }

    std::unique_ptr<Measured> MeasuredFrom(const anim::MmCachedClip& c, std::array<std::vector<float>, 4>& gaitSpeeds)
    {
        auto out          = std::make_unique<Measured>();
        out->rows         = c.features;
        out->frames       = c.frames;
        out->dt           = c.frameDuration;
        out->duration     = c.duration;
        out->looping      = c.loopFrames > 0;
        /// @note A loop's future wraps into its next cycle, so every frame of it is a candidate.
        const int horizon = out->looping ? 0 : static_cast<int>(std::lround(kIgnoreRangeEnd / out->dt));
        for (int i = 0; i < c.frames; ++i) out->searchable.push_back(i + horizon <= c.frames - 1);
        AppendRowGaits(c, *out);
        CollectGaitSpeeds(c, out->dt, out->gait, gaitSpeeds);
        out->schemaRows = c.schemaRows;
        out->speed = WindowedRootSpeeds(c.rootPositions, c.frames, out->dt);
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

    std::string DatabaseSpec::Key() const
    {
        std::string out;
        const auto  text = [&](std::string_view s) { out.append(s), out += '\x1f'; };
        const auto  bits = [&](float f) { out += std::format("{:08x}\x1f", std::bit_cast<std::uint32_t>(f)); };
        for (const SpecChild& c : children) {
            text(c.clip), text(c.database);
            bits(c.rangeStart), bits(c.rangeEnd);
            out += std::to_string(c.flags);
            out += '\x1e';
        }
        out += '\x1d';
        for (const SpecDatabase& d : databases) {
            text(d.name), text(d.baseCostBias), text(d.continuingPoseCostBias), text(d.loopingCostBias);
            text(d.standingCandidate), text(d.movingCandidate);
            out += std::to_string(d.schema) + '\x1f';
            out += d.pivots ? "1\x1e" : "0\x1e";
        }
        out += '\x1d';
        for (const anim::SchemaSpec& s : schemas) text(anim::SchemaText(s));
        out += haveChooser ? "\x1d" "1" : "\x1d" "0";
        for (const SpecChooserRow& r : rows) {
            text(r.when);
            for (const auto& name : r.databases) text(name);
            out += '\x1e';
        }
        out += '\x1d';
        for (const auto& name : fallback) text(name);
        return out;
    }

    anim::SchemaSpec SchemaSpecOf(const SchemaObject& schema, const WarnFn& warn)
    {
        anim::SchemaSpec out;
        out.name     = StrOr(schema, "name");
        out.rootBone = StrOr(schema, "rootBone");
        for (const auto& o : schema.Children("features")) {
            const auto* f = dynamic_cast<const SchemaObject*>(o.get());
            if (!f) continue;
            const std::string_view cls = f->ClassName();
            anim::FeatureSpec      spec;
            spec.weight = f->GetFloat("weight");
            if (cls == "CBPoseSearchTrajectory") {
                spec.kind = anim::FeatureKind::Trajectory;
                for (const auto& so : f->Children("samples"))
                    if (const auto* s = dynamic_cast<const SchemaObject*>(so.get()))
                        spec.samples.push_back({ s->GetFloat("time"), s->GetFloat("position"), s->GetFloat("facing"), s->GetFloat("velocity"),
                                                 s->GetFloat("height") });
            } else if (cls == "CBPoseSearchPosition" || cls == "CBPoseSearchVelocity") {
                spec.kind   = cls == "CBPoseSearchPosition" ? anim::FeatureKind::Position : anim::FeatureKind::Velocity;
                spec.bone   = StrOr(*f, "bone");
                spec.origin = StrOr(*f, "origin");
            } else if (cls == "CBPoseSearchHeading") {
                spec.kind = anim::FeatureKind::Heading;
                spec.bone = StrOr(*f, "bone");
                spec.axis = static_cast<int>(f->GetInt("axis"));
            } else if (cls == "CBPoseSearchCurve") {
                spec.kind  = anim::FeatureKind::Curve;
                spec.curve = StrOr(*f, "curve");
            } else {
                Warn(warn, std::format("schema '{}' holds a {}, which is no feature - ignored", out.name, cls));
                continue;
            }
            out.features.push_back(std::move(spec));
        }
        return out;
    }

    DatabaseSpec SpecOf(const SchemaObject& selector)
    {
        DatabaseSpec spec;
        /// @note A schema shared by several databases is read once: databases name it by its object.
        std::vector<const SchemaObject*> schemaObjects;
        if (const auto* field = selector.HasField("databases") ? selector.FindField("databases") : nullptr)
            for (const auto& o : field->objs) {
                const auto* d = dynamic_cast<const SchemaObject*>(o.get());
                if (!d) continue;
                SpecDatabase p;
                p.name                   = d->Str("name");
                p.baseCostBias           = StrOr(*d, "baseCostBias");
                p.continuingPoseCostBias = StrOr(*d, "continuingPoseCostBias");
                p.loopingCostBias        = StrOr(*d, "loopingCostBias");
                p.standingCandidate      = StrOr(*d, "standingCandidate");
                p.movingCandidate        = StrOr(*d, "movingCandidate");
                if (const auto* tags = d->FindField("tags"))
                    p.pivots = std::find(tags->strs.begin(), tags->strs.end(), "Pivots") != tags->strs.end();
                if (const SchemaObject* s = d->HasField("schema") ? d->Child("schema") : nullptr) {
                    const auto it = std::find(schemaObjects.begin(), schemaObjects.end(), s);
                    p.schema      = static_cast<int>(it - schemaObjects.begin());
                    if (it == schemaObjects.end()) {
                        schemaObjects.push_back(s);
                        spec.schemas.push_back(SchemaSpecOf(*s));
                    }
                }
                spec.databases.push_back(std::move(p));
            }
        for (const auto& o : selector.Children("children")) {
            SpecChild   c;
            const auto* row = dynamic_cast<const SchemaObject*>(o.get());
            if (row) {
                const auto* gen = row->Child("generator");
                if (gen && std::string_view(gen->ClassName()) == "hkbClipGenerator") c.clip = gen->Str("animationName");
                c.database   = StrOr(*row, "database");
                c.rangeStart = row->GetFloat("rangeStart");
                c.rangeEnd   = row->GetFloat("rangeEnd");
                c.flags      = static_cast<std::uint8_t>((row->GetBool("disableReselection") ? kFlagDisableReselection : 0) |
                                                         (row->GetBool("looping") ? kFlagLooping : 0));
            }
            spec.children.push_back(std::move(c));
        }
        if (const SchemaObject* chooser = selector.HasField("chooser") ? selector.Child("chooser") : nullptr) {
            spec.haveChooser = true;
            if (const auto* rows = chooser->FindField("rows"))
                for (const auto& o : rows->objs) {
                    const auto* r = dynamic_cast<const SchemaObject*>(o.get());
                    if (!r) continue;
                    SpecChooserRow row;
                    row.when = StrOr(*r, "when");
                    if (const auto* names = r->FindField("databases")) row.databases = names->strs;
                    spec.rows.push_back(std::move(row));
                }
            if (const auto* fallback = chooser->FindField("fallback")) spec.fallback = fallback->strs;
        }
        return spec;
    }

    void ReadDatabases(const DatabaseSpec& spec, Database& db, const WarnFn& warn)
    {
        const bool implied = spec.databases.empty();
        if (implied) db.databases.push_back({ "(every child)" });
        for (const SpecDatabase& d : spec.databases) {
            PoseDatabase p;
            p.name       = d.name;
            p.base       = CompileExpression(d.baseCostBias, "database '" + p.name + "' baseCostBias", "0", warn);
            p.continuing = CompileExpression(d.continuingPoseCostBias, "database '" + p.name + "' continuingPoseCostBias", "0", warn);
            p.looping    = CompileExpression(d.loopingCostBias, "database '" + p.name + "' loopingCostBias", "0", warn);
            p.standing   = CompileExpression(d.standingCandidate, "database '" + p.name + "' standingCandidate", "no row is a standing candidate", warn);
            p.moving     = CompileExpression(d.movingCandidate, "database '" + p.name + "' movingCandidate", "no row is a moving candidate", warn);
            p.pivots     = d.pivots;
            db.databases.push_back(std::move(p));
        }
        for (std::size_t i = 0; i < spec.children.size(); ++i) {
            const SpecChild& row = spec.children[i];
            Clip&            c   = db.clips[i];
            c.flags      = row.flags;
            c.rangeStart = row.rangeStart;
            c.rangeEnd   = row.rangeEnd;
            if (implied) {
                c.database = 0;
                continue;
            }
            const auto it = std::find_if(db.databases.begin(), db.databases.end(), [&](const PoseDatabase& d) { return d.name == row.database; });
            if (it == db.databases.end()) Warn(warn, std::format("child {} names database '{}', which the graph lacks - never searched", i, row.database));
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

    void ReadChooser(const DatabaseSpec& spec, Database& db, const WarnFn& warn)
    {
        if (!spec.haveChooser) return;
        db.haveChooser = true;
        for (const SpecChooserRow& r : spec.rows) {
            ChooserRow row;
            row.when      = CompileExpression(r.when, "chooser row " + std::to_string(db.chooser.size()) + " `when`", "the row never holds", warn);
            row.databases = DatabasesNamed(db, r.databases, "row", warn);
            db.chooser.push_back(std::move(row));
        }
        db.fallback = DatabasesNamed(db, spec.fallback, "fallback", warn);
    }

    std::unique_ptr<Database> BuildDatabase(const SchemaObject& selector, const anim::Rig* rig, const ClipSource& clips, const WarnFn& warn,
                                            BuildReport* report)
    {
        return BuildDatabase(SpecOf(selector), rig, clips, warn, report);
    }

    std::unique_ptr<Database> BuildDatabase(const DatabaseSpec& spec, const anim::Rig* rig, const ClipSource& clips, const WarnFn& warn,
                                            BuildReport* report)
    {
        auto        db       = std::make_unique<Database>();
        const auto& children = spec.children;
        db->clips.resize(children.size());
        ReadDatabases(spec, *db, warn);
        ReadChooser(spec, *db, warn);

        /// @note The spaces: [0] the classic layout, then each typed schema a database points at (through the rig).
        db->spaces.push_back(ClassicSpace());
        std::vector<int> spaceOfSchema(spec.schemas.size(), 0);
        for (std::size_t k = 0; k < spec.schemas.size(); ++k) {
            if (!rig) {
                Warn(warn, std::format("schema '{}' cannot be measured without the character's rig - its databases weigh the classic layout",
                                       spec.schemas[k].name));
                continue;
            }
            spaceOfSchema[k] = static_cast<int>(db->spaces.size());
            db->spaces.push_back(TypedSpace(spec.schemas[k], *rig));
        }
        for (std::size_t d = 0; d < spec.databases.size(); ++d) {
            const int schema = spec.databases[d].schema;
            if (schema >= 0 && static_cast<std::size_t>(schema) < spaceOfSchema.size()) db->databases[d].space = spaceOfSchema[schema];
        }
        std::vector<char> used(db->spaces.size(), 0);
        for (const PoseDatabase& p : db->databases) used[p.space] = 1;
        ClipNeeds needs;
        needs.rig = rig;
        for (std::size_t sp = 1; sp < db->spaces.size(); ++sp)
            if (used[sp]) needs.schemas.push_back(db->spaces[sp].spec), needs.keys.push_back(db->spaces[sp].key);

        /// @note Keyed by the exact animationName: case is never folded (a differently cased name is its own clip).
        std::map<std::string, std::unique_ptr<Measured>> measured;
        std::vector<const Measured*>                     of(children.size(), nullptr);
        std::array<std::vector<float>, 4>                gaitSpeeds;
        const auto rowsIn = [&](const Measured& m, std::size_t sp) -> const std::vector<float>* {
            if (db->spaces[sp].classic) return &m.rows;
            for (const auto& b : m.schemaRows)
                if (b.key == db->spaces[sp].key) return &b.rows;
            return nullptr;
        };
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (children[i].clip.empty()) {
                Warn(warn, std::format("selector child {} is not a clip generator - never chosen", i));
                continue;
            }
            Clip& c = db->clips[i];
            c.name  = children[i].clip;
            auto it = measured.find(c.name);
            if (it == measured.end()) {
                std::unique_ptr<Measured> m;
                try {
                    m = MeasuredFrom(clips(c.name, needs), gaitSpeeds);
                } catch (const std::exception& e) {
                    Warn(warn, std::format("clip {} '{}' not measured ({}) - never chosen", i, c.name, e.what()));
                }
                /// @note A clip lacking a used space's rows (a bone that schema names is missing) cannot be weighed there,
                ///       nor its pose read there when it plays: it is never chosen.
                for (std::size_t sp = 0; m && sp < db->spaces.size(); ++sp) {
                    if (!used[sp]) continue;
                    const std::vector<float>* rows = rowsIn(*m, sp);
                    if (rows && rows->size() == static_cast<std::size_t>(m->frames) * db->spaces[sp].dims) continue;
                    Warn(warn, std::format("clip {} '{}' has no rows under schema '{}' - never chosen", i, c.name, db->spaces[sp].spec.name));
                    m.reset();
                }
                it = measured.emplace(c.name, std::move(m)).first;
            }
            of[i] = it->second.get();
        }

        /// @note Holden's normalization, per space: each dimension centred on its mean, then each GROUP divided by its mean
        ///       spread, so a group's dimensions keep their relative sizes and every group weighs in comparably. Taken over
        ///       the distinct clips, so a clip listed in several databases does not weigh more.
        std::size_t distinctRows = 0, measuredClips = 0;
        for (const auto& [name, m] : measured) {
            if (!m) continue;
            ++measuredClips;
            distinctRows += static_cast<std::size_t>(m->frames);
        }
        if (distinctRows == 0) return nullptr;
        for (std::size_t sp = 0; sp < db->spaces.size(); ++sp) {
            SchemaSpace& space = db->spaces[sp];
            space.mean.assign(static_cast<std::size_t>(space.dims), 0.0f);
            space.scale.assign(static_cast<std::size_t>(space.dims), 1.0f);
            space.active.clear();
            for (int d = 0; d < space.dims; ++d)
                if (space.weight[d] > 0.0f) space.active.push_back(d);
            if (!used[sp]) continue;
            const int           dims = space.dims;
            std::vector<double> mean(static_cast<std::size_t>(dims), 0.0), var(static_cast<std::size_t>(dims), 0.0);
            for (const auto& [name, m] : measured) {
                if (!m) continue;
                const auto& rows = *rowsIn(*m, sp);
                for (std::size_t k = 0; k < rows.size(); ++k) mean[k % dims] += rows[k];
            }
            for (auto& v : mean) v /= static_cast<double>(distinctRows);
            for (const auto& [name, m] : measured) {
                if (!m) continue;
                const auto& rows = *rowsIn(*m, sp);
                for (std::size_t k = 0; k < rows.size(); ++k) {
                    const double x = rows[k] - mean[k % dims];
                    var[k % dims] += x * x;
                }
            }
            const std::vector<int> groups = NormGroupsOf(space);
            const int              count  = groups.empty() ? 0 : *std::max_element(groups.begin(), groups.end()) + 1;
            std::vector<double>    spread(static_cast<std::size_t>(count), 0.0), members(static_cast<std::size_t>(count), 0.0);
            for (int d = 0; d < dims; ++d) spread[groups[d]] += std::sqrt(var[d] / static_cast<double>(distinctRows)), members[groups[d]] += 1;
            for (int d = 0; d < dims; ++d) {
                const double s = spread[groups[d]] / members[groups[d]];
                space.mean[d]  = static_cast<float>(mean[d]);
                space.scale[d] = static_cast<float>(s > 1.0e-6 ? s : 1.0);
            }
        }

        std::size_t total = 0;
        for (const Measured* m : of) total += m ? static_cast<std::size_t>(m->frames) : 0;
        for (std::size_t sp = 0; sp < db->spaces.size(); ++sp)
            if (used[sp]) db->spaces[sp].rows.reserve(total * db->spaces[sp].dims);
        db->clipOf.reserve(total);
        db->searchable.reserve(total);
        db->rowGates.reserve(total);
        db->rowGait.reserve(total);
        db->rowSpeed.reserve(total);
        /// @note The gates read only the row's curves: the build has no actor, and the context is reset per row so
        ///       the primitive memo never hands one row's curve value to the next.
        relay::EvalContext ctx;
        const auto         gate = [&](const Expression& e) {
            if (e.text.empty()) return true;
            return e.expr.Valid() && e.expr.Evaluate(ctx).AsBool();
        };
        for (std::size_t i = 0; i < db->clips.size(); ++i) {
            Clip& c = db->clips[i];
            c.first = static_cast<int>(db->clipOf.size());
            const Measured* m = of[i];
            if (!m) continue;
            c.frames   = m->frames;
            c.dt       = m->dt;
            c.duration = m->duration;
            if (m->looping) c.flags |= kFlagLooping;
            for (std::size_t sp = 0; sp < db->spaces.size(); ++sp) {
                if (!used[sp]) continue;
                SchemaSpace& space = db->spaces[sp];
                const auto&  rows  = *rowsIn(*m, sp);
                for (std::size_t k = 0; k < rows.size(); ++k) {
                    const int d = static_cast<int>(k % space.dims);
                    space.rows.push_back((rows[k] - space.mean[d]) / space.scale[d]);
                }
            }
            db->clipOf.insert(db->clipOf.end(), static_cast<std::size_t>(m->frames), static_cast<int>(i));
            db->searchable.insert(db->searchable.end(), m->searchable.begin(), m->searchable.end());
            /// @note The range cuts per CHILD: children sharing a clip share its Measured, not its range.
            for (int f = 0; f < m->frames; ++f) {
                const double t = f * m->dt;
                if (t < c.rangeStart || (c.rangeEnd > 0 && t > c.rangeEnd)) db->searchable[c.first + f] = 0;
            }
            db->rowGait.insert(db->rowGait.end(), m->gait.begin(), m->gait.end());
            db->rowSpeed.insert(db->rowSpeed.end(), m->speed.begin(), m->speed.end());
            db->rowSpeed.resize(db->rowGait.size(), 0.0f);
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
        Prepared  p(db, q);
        const int curRow = p.curRow;

        /// @note Each candidate is weighed in its own database's space: Unreal's schema per database.
        const auto cost = [&](std::size_t r, int clip, float bound) {
            const int space = SpaceOf(db, clip);
            return CostIn(db.spaces[space], p.In(space), r, bound);
        };

        /// @note The playing row (the continuing pose) carries its database's continuing bias on top of a candidate's.
        const float playing = curRow >= 0 ? cost(static_cast<std::size_t>(curRow), q.currentClip, 1.0e30f) + BiasOf(db, q, q.currentClip) +
                                                BiasesOf(q, db.clips[q.currentClip].database).continuing
                                          : 1.0e30f;
        float       best     = 1.0e30f;
        int         bestRow  = -1;
        std::size_t searched = 0;
        ForEachCandidate(db, q, curRow, [&](int ci, int r, float b) {
            ++searched;
            const float f = cost(static_cast<std::size_t>(r), ci, best - b);
            if (f + b < best) best = f + b, bestRow = r;
        });

        SearchResult out;
        out.bestRow    = bestRow;
        out.best       = best;
        out.playing    = playing;
        out.playingRow = curRow;
        out.searched   = searched;
        /// @note At a clip's end or an interrupt the playing row cannot go on, so the best candidate wins outright;
        ///       otherwise the pick must be past its dwell and the candidate's biased cost below the playing row's by the
        ///       margin (of its magnitude: a bias can make a cost negative).
        out.wins       = bestRow >= 0 && (curRow < 0 || q.clipEnd || q.interrupt ||
                                    (!q.hold && best < playing - q.margin * std::fabs(playing)));
        return out;
    }

    std::vector<Candidate> TopCandidates(const Database& db, const Query& q, std::size_t n)
    {
        std::vector<Candidate> out;
        if (n == 0) return out;
        Prepared   p(db, q);
        const auto better = [](const Candidate& a, const Candidate& b) { return a.total < b.total || (a.total == b.total && a.row < b.row); };
        ForEachCandidate(db, q, p.curRow, [&](int ci, int r, float b) {
            const int                 space = SpaceOf(db, ci);
            const SchemaSpace&        sp    = db.spaces[space];
            const std::vector<float>& qv    = p.In(space);
            const float*              row   = sp.rows.data() + static_cast<std::size_t>(r) * sp.dims;
            Candidate                 c;
            if (!sp.classic) c.featureCost.assign(sp.spec.features.size(), 0.0f);
            float sum = 0;
            for (const int d : sp.active) {
                const float e    = qv[d] - row[d];
                const float part = sp.weight[d] * e * e;
                sum += part;
                if (sp.classic) c.groupCost[sp.group[d]] += part;
                else c.featureCost[sp.group[d]] += part;
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

    void Matcher::Restart()
    {
        m_clip = m_pendingClip = -1;
        m_time = m_pendingTime = 0;
        m_searchNow = true;
        m_interrupt = false;
        m_interruptActive.clear();
        m_lastSearch = -1.0e9;
        m_lastWantX = m_lastWantY = 0;
        m_prevWantX = m_prevWantY = 0;
        m_lastSwitch = -1.0e9;
        m_picks.clear();
    }

    MatchStep Matcher::Update(const Database& db, const MatchRequest& in, float dt)
    {
        MatchStep out;
        m_clock += dt;
        if (m_pendingClip >= 0) {
            m_clip        = m_pendingClip;
            m_time        = m_pendingTime;
            m_pendingClip = -1;
        } else if (m_clip >= 0) {
            m_time += dt * in.rate;
        }
        if (m_clip >= 0) {
            const Clip& c = db.clips[m_clip];
            /// @note A loop plays on into its next cycle; anything else searches before it runs out.
            if ((c.flags & kFlagLooping) && c.duration > 0) {
                m_time = std::fmod(m_time, c.duration);
            } else {
                if (m_time >= c.duration - kEndMargin) m_searchNow = true;
                m_time = (std::min)(m_time, c.duration);
            }
            if (in.inputsChanged && c.database >= 0 && !IsActive(in.active, c.database)) {
                m_interrupt       = true;
                m_interruptActive = in.active;
            }
        }
        out.clip = m_clip;
        out.time = m_time;

        bool       forcedNow = false;
        const auto due = [&] {
            if (!in.haveQuery) return false;
            const double elapsed = m_clock - m_lastSearch;
            /// @note Forced: the request jumped since the last search, in speed or (while moving) in direction, and has settled.
            ///       Holden forces on the falling edge too: one search for the input it settled on, not one per step of a change.
            const float speed = std::hypot(in.desiredX, in.desiredY), lastSpeed = std::hypot(m_lastWantX, m_lastWantY);
            bool        forced = std::fabs(speed - lastSpeed) > kForceSpeed;
            if (!forced && speed > kForceMoving && lastSpeed > kForceMoving)
                forced = (in.desiredX * m_lastWantX + in.desiredY * m_lastWantY) / (speed * lastSpeed) < kForceCos;
            forced    = forced && std::hypot(in.desiredX - m_prevWantX, in.desiredY - m_prevWantY) < kSettledSpeed;
            forcedNow = forced;
            out.trigger = m_interrupt ? "interrupt" : m_searchNow ? "clip end" : elapsed < kSearchInterval ? "forced" : "timer";
            return m_searchNow || m_interrupt || elapsed >= kSearchInterval || (forced && elapsed >= kForcedSpacing);
        };
        const bool search = due();
        m_prevWantX = in.desiredX;
        m_prevWantY = in.desiredY;
        if (search) {
            /// @note The reselect ban: each pick of the last reselectTime, advanced by the clip time played since (at the
            ///       current rate), bans the rows within kIgnoreSurrounding of where it would be now, as merged row ranges.
            std::erase_if(m_picks, [&](const Pick& p) { return m_clock - p.at > in.reselectTime; });
            Query q;
            for (const Pick& p : m_picks) {
                const Clip&  c  = db.clips[p.clip];
                const double at = p.frame + (m_clock - p.at) * in.rate / c.dt;
                const int    lo = (std::max)(0, static_cast<int>(std::floor(at)) - kIgnoreSurrounding);
                const int    hi = (std::min)(c.frames - 1, static_cast<int>(std::ceil(at)) + kIgnoreSurrounding);
                if (lo <= hi) q.banned.emplace_back(c.first + lo, c.first + hi);
            }
            std::sort(q.banned.begin(), q.banned.end());
            q.samples     = in.samples;
            q.desiredX    = in.desiredX;
            q.desiredY    = in.desiredY;
            q.currentClip = m_clip;
            q.currentTime = m_time;
            q.active      = m_interrupt ? m_interruptActive : in.active;
            q.biases      = in.biases;
            q.clipEnd     = m_searchNow;
            q.interrupt   = m_interrupt;
            q.hold        = m_clock - m_lastSwitch < (forcedNow ? kForcedDwell : kMinDwell);

            m_searchNow  = false;
            m_interrupt  = false;
            m_lastSearch = m_clock;
            m_lastWantX  = in.desiredX;
            m_lastWantY  = in.desiredY;

            const auto t0 = std::chrono::steady_clock::now();
            out.result    = Search(db, q);
            out.searchMs  = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
            out.searched  = true;
            out.searchedActive = std::move(q.active);
            if (out.result.wins) {
                const int   clip = db.clipOf[out.result.bestRow];
                const Clip& c    = db.clips[clip];
                m_pendingClip = clip;
                m_pendingTime = (out.result.bestRow - c.first) * c.dt;
                m_picks.push_back({ clip, static_cast<double>(out.result.bestRow - c.first), m_clock });
                m_lastSwitch = m_clock;
                out.switched = true;
            }
        }

        if (m_pendingClip >= 0) {
            out.nextClip = m_pendingClip;
            out.nextTime = m_pendingTime;
        } else if (m_clip >= 0) {
            out.nextClip     = m_clip;
            const Clip& c    = db.clips[m_clip];
            const double t   = m_time + dt * in.rate;
            out.nextTime     = (c.flags & kFlagLooping) && c.duration > 0 ? std::fmod(t, c.duration) : (std::min)(t, c.duration);
        }
        return out;
    }

}  // namespace CB::engine::locomotion
