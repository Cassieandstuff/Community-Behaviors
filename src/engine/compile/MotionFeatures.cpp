#include "compile/MotionFeatures.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace CB::engine::anim {

    namespace {

        MotionVec operator+(MotionVec a, MotionVec b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        MotionVec operator-(MotionVec a, MotionVec b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        MotionVec operator*(MotionVec a, double s) { return { a.x * s, a.y * s, a.z * s }; }
        MotionVec Cross(MotionVec a, MotionVec b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

        MotionQuat Mul(MotionQuat a, MotionQuat b)
        {
            return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                     a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                     a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
        }
        MotionQuat Normalize(MotionQuat q)
        {
            const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            return n > 0 ? MotionQuat{ q.x / n, q.y / n, q.z / n, q.w / n } : MotionQuat{};
        }

        struct Xf {
            MotionVec  p;
            MotionQuat q;
        };
        /// @brief `local` placed under `parent` (Havok's parent-relative bone transforms).
        Xf Compose(const Xf& parent, const Xf& local) { return { parent.p + Rotate(parent.q, local.p), Normalize(Mul(parent.q, local.q)) }; }

        /// @brief The keys around `t` and the blend between them (held past either end).
        template <class K>
        std::pair<const K*, double> Bracket(const std::vector<K>& keys, double t, const K*& next)
        {
            const auto it = std::upper_bound(keys.begin(), keys.end(), t, [](double v, const K& k) { return v < k.time; });
            if (it == keys.begin()) { next = &keys.front(); return { &keys.front(), 0.0 }; }
            if (it == keys.end())   { next = &keys.back();  return { &keys.back(), 0.0 }; }
            const K& a = *(it - 1);
            next       = &*it;
            const double span = static_cast<double>(it->time) - a.time;
            return { &a, span > 0 ? (t - a.time) / span : 0.0 };
        }
        MotionVec SampleV3(const std::vector<Vec3Keyframe>& keys, double t)
        {
            const Vec3Keyframe* b = nullptr;
            const auto [a, f]     = Bracket(keys, t, b);
            const MotionVec va{ a->value[0], a->value[1], a->value[2] }, vb{ b->value[0], b->value[1], b->value[2] };
            return va + (vb - va) * f;
        }
        MotionQuat SampleQt(const std::vector<QuatKeyframe>& keys, double t)
        {
            const QuatKeyframe* b = nullptr;
            const auto [a, f]     = Bracket(keys, t, b);
            const MotionQuat qa{ a->value[0], a->value[1], a->value[2], a->value[3] };
            MotionQuat       qb{ b->value[0], b->value[1], b->value[2], b->value[3] };
            if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0) qb = { -qb.x, -qb.y, -qb.z, -qb.w };
            return Normalize({ qa.x + (qb.x - qa.x) * f, qa.y + (qb.y - qa.y) * f, qa.z + (qb.z - qa.z) * f, qa.w + (qb.w - qa.w) * f });
        }

        constexpr const char* kRootBone = "NPC Root [Root]";
        constexpr const char* kCom      = "NPC COM [COM ]";
        constexpr const char* kPelvis   = "NPC Pelvis [Pelv]";
        constexpr std::array<std::array<const char*, 3>, 2> kLegs{ {
            { "NPC L Thigh [LThg]", "NPC L Calf [LClf]", "NPC L Foot [Lft ]" },
            { "NPC R Thigh [RThg]", "NPC R Calf [RClf]", "NPC R Foot [Rft ]" },
        } };

        /// @brief Central differences over frames `dt` apart (one-sided at the ends).
        MotionVec Velocity(const std::vector<MotionVec>& p, int i, double dt)
        {
            const int n = static_cast<int>(p.size());
            if (n < 2) return {};
            const int a = i == 0 ? 0 : i - 1, b = i + 1 == n ? n - 1 : i + 1;
            return (p[b] - p[a]) * (1.0 / ((b - a) * dt));
        }

        constexpr double kPi = 3.14159265358979323846;

        /// @brief The measuring constants, as hky-tool's earlier tagger tuned them against the clips (Skyrim units).
        constexpr double kStandSpeed      = 15.0;   ///< units/s: below this the root is standing
        constexpr int    kSmoothHalf      = 3;      ///< root signals: moving average over this many frames each side
        constexpr int    kFootSmoothHalf  = 1;      ///< foot speed: kept short so short contacts survive
        constexpr double kPlantHeight     = 3.0;    ///< a planted foot is at most this far above its clip minimum
        constexpr double kPlantSpeed      = 25.0;   ///< and moves at most this fast (units/s) ...
        constexpr double kPlantSpeedScale = 0.25;   ///< ... or this fraction of the root's speed, whichever is more
        constexpr int    kPlantGapFrames  = 2;      ///< unplanted gaps this short inside a plant are filled
        constexpr int    kPlantMinFrames  = 2;      ///< plants shorter than this are dropped

        /// @brief Central differences per frame (one-sided at the ends), over `dt` seconds per frame.
        std::vector<double> Diff(const std::vector<double>& v, double dt)
        {
            const std::size_t   n = v.size();
            std::vector<double> d(n, 0.0);
            if (n < 2) return d;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t a = i == 0 ? 0 : i - 1, b = i + 1 == n ? n - 1 : i + 1;
                d[i] = (v[b] - v[a]) / (static_cast<double>(b - a) * dt);
            }
            return d;
        }
        std::vector<double> Smooth(const std::vector<double>& v, int half)
        {
            const int           n = static_cast<int>(v.size());
            std::vector<double> s(v.size(), 0.0);
            for (int i = 0; i < n; ++i) {
                const int a = (std::max)(0, i - half), b = (std::min)(n - 1, i + half);
                double    sum = 0;
                for (int j = a; j <= b; ++j) sum += v[j];
                s[i] = sum / (b - a + 1);
            }
            return s;
        }

        /// @brief Per-frame samples as LINEAR keys, each segment as long as every frame inside it stays within `tol`.
        /// @param wraps frames whose next frame jumps across ±180 (null = none): a segment never spans one, and the key
        ///        there is CONSTANT, so the value holds until the jump instead of sweeping through 0.
        AnimCurveDef Simplify(std::string name, const std::vector<double>& v, double dt, double tol,
                              const std::vector<char>* wraps = nullptr)
        {
            AnimCurveDef c{ std::move(name), {} };
            const int    n    = static_cast<int>(v.size());
            const auto   key  = [&](int i, AnimCurveKey::Interp in) {
                c.keys.push_back({ static_cast<float>(i * dt), static_cast<float>(v[i]), in });
            };
            const auto fits = [&](int a, int b) {
                for (int j = a; j < b; ++j)
                    if (wraps && (*wraps)[j]) return false;
                for (int j = a + 1; j < b; ++j)
                    if (std::fabs(v[a] + (v[b] - v[a]) * (j - a) / (b - a) - v[j]) > tol) return false;
                return true;
            };
            int a = 0;
            while (a < n - 1) {
                if (wraps && (*wraps)[a]) {
                    key(a, AnimCurveKey::Interp::Constant);
                    ++a;
                    continue;
                }
                int b = a + 1;
                while (b + 1 < n && fits(a, b + 1)) ++b;
                key(a, AnimCurveKey::Interp::Linear);
                a = b;
            }
            if (n > 0) key(n - 1, AnimCurveKey::Interp::Linear);
            return c;
        }

        /// @brief Per-frame on/off as CONSTANT 0/1 keys: one at the start and one at every change.
        AnimCurveDef Steps(std::string name, const std::vector<char>& on, double dt)
        {
            AnimCurveDef c{ std::move(name), {} };
            for (std::size_t i = 0; i < on.size(); ++i)
                if (i == 0 || on[i] != on[i - 1])
                    c.keys.push_back({ static_cast<float>(i * dt), on[i] ? 1.f : 0.f, AnimCurveKey::Interp::Constant });
            return c;
        }

        /// @brief `on` with short gaps filled, then short runs dropped.
        void Clean(std::vector<char>& on, int gap, int minLen)
        {
            const int n = static_cast<int>(on.size());
            for (int i = 0, end = -1; i < n; ++i) {
                if (!on[i]) continue;
                if (end >= 0 && i - end <= gap)
                    for (int j = end; j < i; ++j) on[j] = 1;
                while (i < n && on[i]) ++i;
                end = i;
            }
            for (int i = 0; i < n;) {
                if (!on[i]) { ++i; continue; }
                int j = i;
                while (j < n && on[j]) ++j;
                if (j - i < minLen)
                    for (int k = i; k < j; ++k) on[k] = 0;
                i = j;
            }
        }

    }  // namespace

    MotionVec Rotate(const MotionQuat& q, const MotionVec& v)
    {
        const MotionVec u{ q.x, q.y, q.z };
        const MotionVec t = Cross(u, v) * 2.0;
        return v + t * q.w + Cross(u, t);
    }

    MotionVec ToLocal(const MotionQuat& q, const MotionVec& v) { return Rotate({ -q.x, -q.y, -q.z, q.w }, v); }

    std::optional<ClipMotion> MeasureClip(const AnimationDef& def, std::string& err)
    {
        ClipMotion m;
        m.dt           = def.frameDuration > 0 ? def.frameDuration : 1.0 / 30.0;
        const int n    = def.numFrames > 0 ? def.numFrames : static_cast<int>(std::lround(def.duration / m.dt)) + 1;
        if (n < 2) { err = "fewer than 2 frames"; return std::nullopt; }
        m.frames = n;

        std::unordered_map<std::string, const AnimTrackDef*> tracks;
        for (const auto& t : def.tracks) tracks.emplace(t.bone, &t);
        const auto bone = [&](const char* name) -> const AnimTrackDef* {
            const auto it = tracks.find(name);
            return it == tracks.end() || it->second->translation.empty() || it->second->rotation.empty() ? nullptr : it->second;
        };
        const auto* root = bone(kRootBone);
        if (!root) { err = std::string("no translation + rotation track on ") + kRootBone; return std::nullopt; }
        const auto* com    = bone(kCom);
        const auto* pelvis = bone(kPelvis);
        std::array<std::array<const AnimTrackDef*, 3>, 2> legs{};
        m.hasHip  = com && pelvis;
        m.hasFeet = m.hasHip;
        for (int s = 0; s < 2; ++s)
            for (int b = 0; b < 3; ++b) {
                legs[s][b] = bone(kLegs[s][b]);
                if (!legs[s][b]) m.hasFeet = false;
            }

        m.rootPos.resize(n);
        m.rootRot.resize(n);
        if (m.hasHip) m.hip.resize(n);
        if (m.hasFeet) {
            m.foot  = { std::vector<MotionVec>(n), std::vector<MotionVec>(n) };
            m.thigh = { std::vector<MotionVec>(n), std::vector<MotionVec>(n) };
        }
        const auto local = [](const AnimTrackDef* tr, double t) { return Xf{ SampleV3(tr->translation, t), SampleQt(tr->rotation, t) }; };
        for (int i = 0; i < n; ++i) {
            const double t     = i * m.dt;
            const Xf     rootX = local(root, t);
            m.rootPos[i]       = rootX.p;
            m.rootRot[i]       = rootX.q;
            if (!m.hasHip) continue;
            const Xf pelvisX = Compose(Compose(rootX, local(com, t)), local(pelvis, t));
            m.hip[i]         = pelvisX.p;
            if (!m.hasFeet) continue;
            for (int s = 0; s < 2; ++s) {
                Xf w = pelvisX;
                for (std::size_t b = 0; b < legs[s].size(); ++b) {
                    w = Compose(w, local(legs[s][b], t));
                    if (b == 0) m.thigh[s][i] = w.p;
                }
                m.foot[s][i] = w.p;
            }
        }
        return m;
    }

    namespace {

        /// @brief Frames: a shorter clip is never taken for a loop (too little motion to tell its seam).
        constexpr int kMinLoopFrames = 8;
        /// @brief How close the last frame's pose must come to the first's to call the clip a loop: feet within
        ///        kLoopFootPos plus kLoopSteps frames of their travel, foot velocities within kLoopFootVel or kLoopVelShare
        ///        of their speed, the pelvis velocity within kLoopHipVel. kLoopRepeat: the last frame repeats the first.
        /// @note  Tuned on GASP's clips (CB_TEST_MM_CLIPS in engine-tests prints every clip's fit).
        constexpr double kLoopFootPos = 3.0, kLoopSteps = 1.5, kLoopFootVel = 100.0, kLoopVelShare = 0.5, kLoopHipVel = 40.0,
                         kLoopRepeat = 1.0;
        /// @brief A clip whose pelvis moves slower than kLoopStill (game units/s) at both ends stands there, and loops only if
        ///        its root travels less than kLoopTravel (game units) end to end. A clip turning more than kLoopTurn (radians)
        ///        end to end loops only if it already turns at its ends at kLoopCurve of its average rate (an arc, a circle):
        ///        a turn clip turns in its middle, its ends straight. Each end's turn rate is read over kLoopEndTime seconds.
        constexpr double kLoopStill = 60.0, kLoopTravel = 20.0, kLoopTurn = 0.26, kLoopCurve = 0.5, kLoopEndTime = 0.2;
        /// @brief Game units the root may leave the straight line between its end heights: a loop is level, or climbs evenly
        ///        (a slope); a vault or a climb rises and falls in its middle.
        constexpr double kLoopHeight = 10.0;

        double YawOf(const MotionQuat& q)
        {
            const MotionVec f = Rotate(q, { 0, 1, 0 });
            return std::atan2(-f.x, f.y);
        }
        double WrapAngle(double a)
        {
            while (a > kPi) a -= 2 * kPi;
            while (a < -kPi) a += 2 * kPi;
            return a;
        }

        MotionQuat Conj(MotionQuat q) { return { -q.x, -q.y, -q.z, q.w }; }

        /// @brief The root at frames [-before, frames + after), cycles laid end to end: frame k + period is frame k carried
        ///        by one cycle's motion (the transform from frame 0 to frame `period`).
        /// @note  A loop whose last frame runs on into the first (period == frames) takes its seam frame from the last
        ///        frame's own step.
        void Unroll(const ClipMotion& m, int period, int before, int after, std::vector<MotionVec>& pos, std::vector<MotionQuat>& rot)
        {
            const int  n = m.frames;
            MotionVec  endPos;
            MotionQuat endRot;
            if (period < n) {
                endPos = m.rootPos[period];
                endRot = m.rootRot[period];
            } else {
                const MotionQuat stepRot = Mul(Conj(m.rootRot[n - 2]), m.rootRot[n - 1]);
                const MotionVec  stepPos = ToLocal(m.rootRot[n - 2], m.rootPos[n - 1] - m.rootPos[n - 2]);
                endRot = Normalize(Mul(m.rootRot[n - 1], stepRot));
                endPos = m.rootPos[n - 1] + Rotate(m.rootRot[n - 1], stepPos);
            }
            const MotionQuat cycleRot = Normalize(Mul(endRot, Conj(m.rootRot[0])));
            const MotionVec  cyclePos = endPos - Rotate(cycleRot, m.rootPos[0]);
            const int        total    = before + n + after;
            pos.resize(static_cast<std::size_t>(total));
            rot.resize(static_cast<std::size_t>(total));
            for (int k = -before; k < n + after; ++k) {
                int        r = k, cycles = 0;
                while (r < 0) r += period, --cycles;
                while (r >= period) r -= period, ++cycles;
                MotionVec  p = m.rootPos[r];
                MotionQuat q = m.rootRot[r];
                for (; cycles > 0; --cycles) p = Rotate(cycleRot, p) + cyclePos, q = Normalize(Mul(cycleRot, q));
                for (; cycles < 0; ++cycles) p = Rotate(Conj(cycleRot), p - cyclePos), q = Normalize(Mul(Conj(cycleRot), q));
                pos[k + before] = p;
                rot[k + before] = q;
            }
        }

    }  // namespace

    void AppendRows(const ClipMotion& m, const std::array<int, features::kSamples>& offset, const std::vector<MotionVec>& rootPos,
                    const std::vector<MotionQuat>& rootRot, int lead, float* out);

    LoopFit FitLoop(const float* rows, int frames, double dt)
    {
        using namespace features;
        LoopFit fit;
        if (!rows || frames < kMinLoopFrames || dt <= 0) return fit;
        const float* a = rows;
        const float* b = rows + static_cast<std::size_t>(frames - 1) * kDims;
        for (int k = 0; k < 6; ++k) {
            fit.footPos = (std::max)(fit.footPos, static_cast<double>(std::fabs(a[kFootPos + k] - b[kFootPos + k])));
            fit.footVel = (std::max)(fit.footVel, static_cast<double>(std::fabs(a[kFootVel + k] - b[kFootVel + k])));
        }
        for (int k = 0; k < 3; ++k) fit.hipVel = (std::max)(fit.hipVel, static_cast<double>(std::fabs(a[kHipVel + k] - b[kHipVel + k])));
        double speed = 0;
        for (int k = 0; k < 6; ++k) speed = (std::max)(speed, static_cast<double>((std::max)(std::fabs(a[kFootVel + k]), std::fabs(b[kFootVel + k]))));
        fit.step = speed * dt;
        const bool loops = fit.footPos <= kLoopFootPos + kLoopSteps * fit.step && fit.footVel <= (std::max)(kLoopFootVel, kLoopVelShare * speed) &&
                           fit.hipVel <= kLoopHipVel;
        if (loops) fit.period = fit.footPos <= kLoopRepeat + 0.5 * fit.step ? frames - 1 : frames;
        return fit;
    }

    int LoopPeriod(const ClipMotion& m, const float* rows)
    {
        using namespace features;
        const LoopFit fit = FitLoop(rows, m.frames, m.dt);
        if (fit.period <= 0) return 0;
        const int    n     = m.frames;
        const float* first = rows;
        const float* last  = rows + static_cast<std::size_t>(n - 1) * kDims;
        const double still = (std::max)(std::hypot(first[kHipVel], first[kHipVel + 1]), std::hypot(last[kHipVel], last[kHipVel + 1]));
        const MotionVec d  = m.rootPos[n - 1] - m.rootPos[0];
        if (still < kLoopStill && std::hypot(d.x, d.y) >= kLoopTravel) return 0;
        for (int i = 1; i + 1 < n; ++i) {
            const double line = m.rootPos[0].z + (m.rootPos[n - 1].z - m.rootPos[0].z) * i / (n - 1);
            if (std::fabs(m.rootPos[i].z - line) > kLoopHeight) return 0;
        }
        /// @note The total turn follows the root frame by frame, so a turn past 180 degrees still counts in full.
        double total = 0;
        for (int i = 1; i < n; ++i) total += WrapAngle(YawOf(m.rootRot[i]) - YawOf(m.rootRot[i - 1]));
        if (std::fabs(total) < kLoopTurn) return fit.period;
        const int    k        = (std::clamp)(static_cast<int>(std::lround(kLoopEndTime / m.dt)), 1, (n - 1) / 2);
        const double startRate = WrapAngle(YawOf(m.rootRot[k]) - YawOf(m.rootRot[0])) / (k * m.dt);
        const double endRate   = WrapAngle(YawOf(m.rootRot[n - 1]) - YawOf(m.rootRot[n - 1 - k])) / (k * m.dt);
        const double average   = std::fabs(total) / ((n - 1) * m.dt);
        return (std::min)(std::fabs(startRate), std::fabs(endRate)) >= kLoopCurve * average ? fit.period : 0;
    }

    int AppendClipFeatures(const ClipMotion& m, std::vector<float>& rows)
    {
        using namespace features;
        const int n = m.frames;
        std::array<int, kSamples> offset{};
        for (int s = 0; s < kSamples; ++s) offset[s] = static_cast<int>(std::lround(SampleTime(s) / m.dt));
        const std::size_t base = rows.size();
        rows.resize(base + static_cast<std::size_t>(n) * kDims);
        AppendRows(m, offset, m.rootPos, m.rootRot, 0, rows.data() + base);
        /// @note A second pass for a loop: its pose rows stand, its trajectory samples are taken again across the seam.
        const int period = LoopPeriod(m, rows.data() + base);
        if (period <= 0) return 0;
        int ahead = 0, back = 0;
        for (int s = 0; s < kSamples; ++s) ahead = (std::max)(ahead, offset[s]), back = (std::max)(back, -offset[s]);
        std::vector<MotionVec>  pos;
        std::vector<MotionQuat> rot;
        Unroll(m, period, back + 1, ahead + 1, pos, rot);
        AppendRows(m, offset, pos, rot, back + 1, rows.data() + base);
        return period;
    }

    void AppendRows(const ClipMotion& m, const std::array<int, features::kSamples>& offset, const std::vector<MotionVec>& rootPos,
                    const std::vector<MotionQuat>& rootRot, int lead, float* out)
    {
        using namespace features;
        const int n    = m.frames;
        const int span = static_cast<int>(rootPos.size());
        for (int i = 0; i < n; ++i) {
            float* r = out + static_cast<std::size_t>(i) * kDims;
            const MotionQuat& q = rootRot[i + lead];
            const MotionVec&  p = rootPos[i + lead];
            for (int s = 0; s < kSamples; ++s) {
                const int       j   = std::clamp(i + lead + offset[s], 0, span - 1);
                const MotionVec pos = ToLocal(q, rootPos[j] - p);
                const MotionVec dir = ToLocal(q, Rotate(rootRot[j], { 0, 1, 0 }));
                const MotionVec vel = ToLocal(q, Velocity(rootPos, j, m.dt));
                const double    len = std::hypot(dir.x, dir.y);
                float*          o   = r + SampleAt(s);
                o[kSamplePos]          = static_cast<float>(pos.x);
                o[kSamplePos + 1]      = static_cast<float>(pos.y);
                o[kSampleFacing]       = static_cast<float>(len > 0 ? dir.x / len : 0.0);
                o[kSampleFacing + 1]   = static_cast<float>(len > 0 ? dir.y / len : 1.0);
                o[kSampleVelocity]     = static_cast<float>(vel.x);
                o[kSampleVelocity + 1] = static_cast<float>(vel.y);
                o[kSampleHeight]       = static_cast<float>(pos.z);
            }
            if (lead > 0) continue;   // the loop's second pass: only the trajectory changes
            for (int s = 0; s < 2; ++s) {
                const MotionVec fp = ToLocal(q, m.foot[s][i] - p);
                const MotionVec fv = ToLocal(q, Velocity(m.foot[s], i, m.dt));
                const float     pv[3] = { static_cast<float>(fp.x), static_cast<float>(fp.y), static_cast<float>(fp.z) };
                const float     vv[3] = { static_cast<float>(fv.x), static_cast<float>(fv.y), static_cast<float>(fv.z) };
                std::copy(pv, pv + 3, r + kFootPos + 3 * s);
                std::copy(vv, vv + 3, r + kFootVel + 3 * s);
            }
            const MotionVec hv = ToLocal(q, Velocity(m.hip, i, m.dt));
            r[kHipVel]     = static_cast<float>(hv.x);
            r[kHipVel + 1] = static_cast<float>(hv.y);
            r[kHipVel + 2] = static_cast<float>(hv.z);
            const MotionVec spacing = ToLocal(q, m.foot[0][i] - m.foot[1][i]);
            r[kFootSpacing]     = static_cast<float>(spacing.x);
            r[kFootSpacing + 1] = static_cast<float>(spacing.y);
            r[kFootSpacing + 2] = static_cast<float>(spacing.z);
            /// @note The hips' forward from the line between the thighs, not a pelvis bone axis: that line is level in
            ///       every humanoid rig, where a bone's own axes depend on how the rig was built.
            const MotionVec across = ToLocal(q, m.thigh[0][i] - m.thigh[1][i]);
            const double    len    = std::hypot(across.x, across.y);
            r[kPelvisFacing]     = static_cast<float>(len > 0 ? across.y / len : 0.0);
            r[kPelvisFacing + 1] = static_cast<float>(len > 0 ? -across.x / len : 1.0);
        }
    }

    std::vector<AnimCurveDef> MeasureMotionCurves(const ClipMotion& m, const CurveTolerances& tol)
    {
        const int    n  = m.frames;
        const double dt = m.dt;

        std::vector<double> px(n), py(n), yaw(n);
        for (int i = 0; i < n; ++i) {
            px[i]               = m.rootPos[i].x;
            py[i]               = m.rootPos[i].y;
            const MotionVec f   = Rotate(m.rootRot[i], { 0, 1, 0 });
            yaw[i]              = std::atan2(-f.x, f.y);
            if (i > 0) {
                while (yaw[i] - yaw[i - 1] > kPi) yaw[i] -= 2 * kPi;
                while (yaw[i] - yaw[i - 1] < -kPi) yaw[i] += 2 * kPi;
            }
        }
        const auto vx = Smooth(Diff(px, dt), kSmoothHalf), vy = Smooth(Diff(py, dt), kSmoothHalf);
        std::vector<double> speed(n), direction(n);
        int                 firstMoving = -1;
        for (int i = 0; i < n; ++i) {
            speed[i] = std::hypot(vx[i], vy[i]);
            if (speed[i] < kStandSpeed) {
                direction[i] = i > 0 ? direction[i - 1] : 0.0;
                continue;
            }
            const MotionVec lv = ToLocal(m.rootRot[i], { vx[i], vy[i], 0 });
            direction[i]       = std::atan2(lv.x, lv.y) * 180.0 / kPi;
            if (firstMoving < 0) firstMoving = i;
        }
        for (int i = 0; i < firstMoving; ++i) direction[i] = direction[firstMoving];

        /// @note yaw grows counter-clockwise (to the left); TurnRate is + = right, like MoveDirection and Skyrim's heading.
        auto turnRate = Smooth(Diff(yaw, dt), kSmoothHalf);
        for (auto& y : turnRate) y *= -180.0 / kPi;
        const auto accel = Smooth(Diff(speed, dt), kSmoothHalf);

        std::vector<char> wraps(n, 0);
        for (int i = 0; i + 1 < n; ++i) wraps[i] = std::fabs(direction[i + 1] - direction[i]) > 180.0;

        std::vector<AnimCurveDef> out;
        out.push_back(Simplify("Speed", speed, dt, tol.speed));
        out.push_back(Simplify("MoveDirection", direction, dt, tol.moveDirection, &wraps));
        out.push_back(Simplify("TurnRate", turnRate, dt, tol.turnRate));
        out.push_back(Simplify("Acceleration", accel, dt, tol.acceleration));
        if (!m.hasFeet) return out;

        static constexpr const char* kLockNames[2] = { "LeftFootLock", "RightFootLock" };
        for (int s = 0; s < 2; ++s) {
            const auto& foot = m.foot[s];
            double      minZ = foot[0].z;
            for (const auto& p : foot) minZ = (std::min)(minZ, p.z);
            std::vector<double> v(n);
            for (int i = 0; i < n; ++i) {
                const MotionVec d = Velocity(foot, i, dt);
                v[i]              = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            }
            v = Smooth(v, kFootSmoothHalf);
            std::vector<char> on(n);
            /// @note The speed limit grows with the root's speed: at a run or sprint the ankle keeps rolling through a
            ///       contact (10-90 units/s measured), so a fixed walk-tuned limit missed nearly every fast contact.
            for (int i = 0; i < n; ++i)
                on[i] = foot[i].z - minZ <= kPlantHeight && v[i] <= (std::max)(kPlantSpeed, kPlantSpeedScale * speed[i]);
            Clean(on, kPlantGapFrames, kPlantMinFrames);
            out.push_back(Steps(kLockNames[s], on, dt));
        }
        return out;
    }

    int Rig::IndexOf(std::string_view name) const
    {
        for (std::size_t i = 0; i < bones.size(); ++i)
            if (bones[i].name == name) return static_cast<int>(i);
        return -1;
    }

    int FeatureDims(const FeatureSpec& f)
    {
        switch (f.kind) {
        case FeatureKind::Trajectory: return static_cast<int>(f.samples.size()) * features::kPerSample;
        case FeatureKind::Position:
        case FeatureKind::Velocity: return 3;
        case FeatureKind::Heading: return 2;
        case FeatureKind::Curve: return 1;
        }
        return 0;
    }

    int SchemaDims(const SchemaSpec& s)
    {
        int n = 0;
        for (const auto& f : s.features) n += FeatureDims(f);
        return n;
    }

    std::vector<float> SchemaWeights(const SchemaSpec& s)
    {
        using namespace features;
        std::vector<float> w;
        w.reserve(static_cast<std::size_t>(SchemaDims(s)));
        for (const auto& f : s.features) {
            if (f.kind != FeatureKind::Trajectory) {
                w.insert(w.end(), static_cast<std::size_t>(FeatureDims(f)), f.weight);
                continue;
            }
            for (const auto& smp : f.samples) {
                float part[kPerSample]{};
                part[kSamplePos] = part[kSamplePos + 1] = smp.position;
                part[kSampleFacing] = part[kSampleFacing + 1] = smp.facing;
                part[kSampleVelocity] = part[kSampleVelocity + 1] = smp.velocity;
                part[kSampleHeight] = smp.height;
                for (float p : part) w.push_back(f.weight * p);
            }
        }
        return w;
    }

    std::optional<std::vector<float>> MeasureSchema(const AnimationDef& def, const Rig& rig, const SchemaSpec& schema, int loopFrames,
                                                    std::string& err)
    {
        using namespace features;
        const double dt = def.frameDuration > 0 ? def.frameDuration : 1.0 / 30.0;
        const int    n  = def.numFrames > 0 ? def.numFrames : static_cast<int>(std::lround(def.duration / dt)) + 1;
        if (n < 2) { err = "fewer than 2 frames"; return std::nullopt; }
        if (rig.bones.empty()) { err = "an empty rig"; return std::nullopt; }

        const int root = schema.rootBone.empty() ? 0 : rig.IndexOf(schema.rootBone);
        if (root < 0) { err = "the rig has no root bone '" + schema.rootBone + "'"; return std::nullopt; }
        const auto boneOf = [&](const std::string& name, int fallback) -> int {
            if (name.empty()) return fallback;
            const int b = rig.IndexOf(name);
            if (b < 0) err = "the rig has no bone '" + name + "'";
            return b;
        };
        std::vector<int> bone(schema.features.size(), -1), origin(schema.features.size(), -1);
        for (std::size_t k = 0; k < schema.features.size(); ++k) {
            const FeatureSpec& f = schema.features[k];
            if (f.kind == FeatureKind::Position || f.kind == FeatureKind::Velocity || f.kind == FeatureKind::Heading) {
                bone[k] = boneOf(f.bone, -1);
                if (f.bone.empty()) err = "a " + std::string(f.kind == FeatureKind::Heading ? "heading" : "bone") + " feature names no bone";
                origin[k] = boneOf(f.origin, root);
                if (bone[k] < 0 || origin[k] < 0) return std::nullopt;
            }
        }

        /// @note Model space per frame, through the rig's parents: an animated bone's track, else its reference pose.
        std::unordered_map<std::string, const AnimTrackDef*> tracks;
        for (const auto& t : def.tracks) tracks.emplace(t.bone, &t);
        std::vector<const AnimTrackDef*> trackOf(rig.bones.size(), nullptr);
        for (std::size_t b = 0; b < rig.bones.size(); ++b)
            if (const auto it = tracks.find(rig.bones[b].name); it != tracks.end()) trackOf[b] = it->second;
        std::vector<std::vector<Xf>> world(rig.bones.size());
        std::vector<char>            busy(rig.bones.size(), 0);
        const auto solve = [&](auto&& self, int b) -> bool {
            if (!world[b].empty()) return true;
            if (busy[b]) { err = "the rig's parents loop at '" + rig.bones[b].name + "'"; return false; }
            busy[b] = 1;
            const int parent = rig.bones[b].parent;
            if (parent >= static_cast<int>(rig.bones.size())) { err = "a parent index past the rig"; return false; }
            if (parent >= 0 && !self(self, parent)) return false;
            const Rig::Bone&    rb = rig.bones[b];
            const AnimTrackDef* tr = trackOf[b];
            world[b].resize(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                const double t = i * dt;
                Xf local{ rb.translation, rb.rotation };
                if (tr && !tr->translation.empty()) local.p = SampleV3(tr->translation, t);
                if (tr && !tr->rotation.empty()) local.q = SampleQt(tr->rotation, t);
                world[b][i] = parent >= 0 ? Compose(world[parent][i], local) : local;
            }
            return true;
        };
        if (!solve(solve, root)) return std::nullopt;
        for (std::size_t k = 0; k < schema.features.size(); ++k) {
            if (bone[k] >= 0 && !solve(solve, bone[k])) return std::nullopt;
            if (origin[k] >= 0 && !solve(solve, origin[k])) return std::nullopt;
        }

        ClipMotion m;
        m.dt     = dt;
        m.frames = n;
        for (const Xf& x : world[root]) m.rootPos.push_back(x.p), m.rootRot.push_back(x.q);
        int ahead = 0, back = 0;
        for (const auto& f : schema.features)
            for (const auto& smp : f.samples) {
                const int o = static_cast<int>(std::lround(smp.time / dt));
                ahead = (std::max)(ahead, o), back = (std::max)(back, -o);
            }
        /// @note A loop's root runs on across its seam (Unroll); anything else's holds at its ends.
        std::vector<MotionVec>  path    = m.rootPos;
        std::vector<MotionQuat> pathRot = m.rootRot;
        int                     lead    = 0;
        if (loopFrames > 0 && loopFrames <= n) {
            Unroll(m, loopFrames, back + 1, ahead + 1, path, pathRot);
            lead = back + 1;
        }
        const int span = static_cast<int>(path.size());

        const int        dims = SchemaDims(schema);
        std::vector<float> rows(static_cast<std::size_t>(n) * dims, 0.0f);
        /// @brief Central differences in model space (one-sided at the ends), as Velocity.
        const auto velocityOf = [&](int b, int i) {
            const int a = i == 0 ? 0 : i - 1, c = i + 1 == n ? n - 1 : i + 1;
            return (world[b][c].p - world[b][a].p) * (1.0 / ((c - a) * dt));
        };
        for (int i = 0; i < n; ++i) {
            float*            r = rows.data() + static_cast<std::size_t>(i) * dims;
            const MotionQuat& q = m.rootRot[i];
            const MotionVec&  p = m.rootPos[i];
            for (std::size_t k = 0; k < schema.features.size(); ++k) {
                const FeatureSpec& f = schema.features[k];
                switch (f.kind) {
                case FeatureKind::Trajectory:
                    for (const auto& smp : f.samples) {
                        const int       j   = std::clamp(i + lead + static_cast<int>(std::lround(smp.time / dt)), 0, span - 1);
                        const MotionVec pos = ToLocal(q, path[j] - p);
                        const MotionVec dir = ToLocal(q, Rotate(pathRot[j], { 0, 1, 0 }));
                        const MotionVec vel = ToLocal(q, Velocity(path, j, dt));
                        const double    len = std::hypot(dir.x, dir.y);
                        r[kSamplePos]          = static_cast<float>(pos.x);
                        r[kSamplePos + 1]      = static_cast<float>(pos.y);
                        r[kSampleFacing]       = static_cast<float>(len > 0 ? dir.x / len : 0.0);
                        r[kSampleFacing + 1]   = static_cast<float>(len > 0 ? dir.y / len : 1.0);
                        r[kSampleVelocity]     = static_cast<float>(vel.x);
                        r[kSampleVelocity + 1] = static_cast<float>(vel.y);
                        r[kSampleHeight]       = static_cast<float>(pos.z);
                        r += kPerSample;
                    }
                    break;
                case FeatureKind::Position: {
                    const MotionVec v = ToLocal(q, world[bone[k]][i].p - world[origin[k]][i].p);
                    r[0] = static_cast<float>(v.x), r[1] = static_cast<float>(v.y), r[2] = static_cast<float>(v.z);
                    r += 3;
                    break;
                }
                case FeatureKind::Velocity: {
                    const MotionVec v = ToLocal(q, velocityOf(bone[k], i) - velocityOf(origin[k], i));
                    r[0] = static_cast<float>(v.x), r[1] = static_cast<float>(v.y), r[2] = static_cast<float>(v.z);
                    r += 3;
                    break;
                }
                case FeatureKind::Heading: {
                    const MotionVec unit = f.axis == 0 ? MotionVec{ 1, 0, 0 } : f.axis == 2 ? MotionVec{ 0, 0, 1 } : MotionVec{ 0, 1, 0 };
                    const MotionVec h    = ToLocal(q, Rotate(world[bone[k]][i].q, unit));
                    const double    len  = std::hypot(h.x, h.y);
                    r[0] = static_cast<float>(len > 0 ? h.x / len : 0.0);
                    r[1] = static_cast<float>(len > 0 ? h.y / len : 1.0);
                    r += 2;
                    break;
                }
                case FeatureKind::Curve: {
                    const AnimCurveDef* c = nullptr;
                    for (const auto& cv : def.curves)
                        if (cv.name == f.curve) c = &cv;
                    r[0] = c ? SampleCurve(*c, static_cast<float>(i * dt)) : 0.0f;
                    r += 1;
                    break;
                }
                }
            }
        }
        return rows;
    }

}  // namespace CB::engine::anim
