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
        if (m.hasFeet) m.foot = { std::vector<MotionVec>(n), std::vector<MotionVec>(n) };
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
                for (const auto* tr : legs[s]) w = Compose(w, local(tr, t));
                m.foot[s][i] = w.p;
            }
        }
        return m;
    }

    void AppendClipFeatures(const ClipMotion& m, std::vector<float>& rows)
    {
        using namespace features;
        const int n = m.frames;
        std::array<int, kHorizons.size()> ahead{};
        for (std::size_t k = 0; k < kHorizons.size(); ++k) ahead[k] = static_cast<int>(std::lround(kHorizons[k] / m.dt));
        const std::size_t base = rows.size();
        rows.resize(base + static_cast<std::size_t>(n) * kDims);
        for (int i = 0; i < n; ++i) {
            float* r = rows.data() + base + static_cast<std::size_t>(i) * kDims;
            const MotionQuat& q = m.rootRot[i];
            const MotionVec&  p = m.rootPos[i];
            for (std::size_t k = 0; k < kHorizons.size(); ++k) {
                const int       j   = (std::min)(i + ahead[k], n - 1);
                const MotionVec pos = ToLocal(q, m.rootPos[j] - p);
                const MotionVec dir = ToLocal(q, Rotate(m.rootRot[j], { 0, 1, 0 }));
                const double    len = std::hypot(dir.x, dir.y);
                r[kTrajPos + 2 * k]     = static_cast<float>(pos.x);
                r[kTrajPos + 2 * k + 1] = static_cast<float>(pos.y);
                r[kTrajDir + 2 * k]     = static_cast<float>(len > 0 ? dir.x / len : 0.0);
                r[kTrajDir + 2 * k + 1] = static_cast<float>(len > 0 ? dir.y / len : 1.0);
            }
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
        }
    }

}  // namespace CB::engine::anim
