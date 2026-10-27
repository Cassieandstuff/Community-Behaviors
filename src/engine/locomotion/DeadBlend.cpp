#include <locomotion/DeadBlend.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace CB::engine::locomotion {

    namespace {

        inline Quat Mul(const Quat& a, const Quat& b)
        {
            return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
        }
        inline Quat Conj(const Quat& q) { return { -q.x, -q.y, -q.z, q.w }; }
        inline Quat Normalize(const Quat& q)
        {
            const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            return n > 1.0e-8f ? Quat{ q.x / n, q.y / n, q.z / n, q.w / n } : Quat{};
        }
        /// @brief The rotation by |(x, y, z)| radians about (x, y, z).
        inline Quat FromScaledAxis(float x, float y, float z)
        {
            const float angle = std::sqrt(x * x + y * y + z * z);
            if (angle < 1.0e-6f) return Normalize({ 0.5f * x, 0.5f * y, 0.5f * z, 1.0f });
            const float s = std::sin(0.5f * angle) / angle;
            return { x * s, y * s, z * s, std::cos(0.5f * angle) };
        }
        /// @brief Shortest-path slerp from `a` to `b`.
        inline Quat Slerp(const Quat& a, Quat b, float t)
        {
            float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (d < 0.0f) b = { -b.x, -b.y, -b.z, -b.w }, d = -d;
            if (d > 0.9995f)
                return Normalize({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
            const float th = std::acos(d), s = std::sin(th);
            const float wa = std::sin((1.0f - t) * th) / s, wb = std::sin(t * th) / s;
            return { a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb };
        }
        /// @brief How far a velocity decaying with `halflife` carries over `t`: the integral of e^(-lambda s) on [0, t].
        inline float Coast(float t, float halflife)
        {
            const float lambda = 0.69314718f / halflife;
            return (1.0f - std::exp(-lambda * t)) / lambda;
        }

    }  // namespace

    bool FiniteBone(const Bone& b)
    {
        for (int a = 0; a < 3; a++)
            if (!std::isfinite(b.t[a])) return false;
        return std::isfinite(b.q.x) && std::isfinite(b.q.y) && std::isfinite(b.q.z) && std::isfinite(b.q.w);
    }

    void DeadBlend::Reset(int n, int playing)
    {
        bones = n, child = playing, history = 0, blending = false, t = 0.0f;
        last.assign(static_cast<std::size_t>(n), Bone{});
        prev.assign(static_cast<std::size_t>(n), Bone{});
    }

    void DeadBlend::Record(const Bone* pose)
    {
        prev.swap(last);
        for (int i = 0; i < bones; i++) last[i] = pose[i];
        history = (std::min)(history + 1, 2);
    }

    void DeadBlend::Start()
    {
        from = last;
        vel.assign(static_cast<std::size_t>(bones), BoneVelocity{});
        const float inv = dt > 1.0e-5f ? 1.0f / dt : 60.0f;
        for (int i = 0; i < bones; i++) {
            BoneVelocity& v = vel[i];
            for (int a = 0; a < 3; a++) v.t[a] = std::clamp((last[i].t[a] - prev[i].t[a]) * inv, -kMaxLinear, kMaxLinear);
            Quat dq = Mul(last[i].q, Conj(prev[i].q));
            if (dq.w < 0.0f) dq = { -dq.x, -dq.y, -dq.z, -dq.w };
            const float sn    = std::sqrt(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z);
            const float angle = 2.0f * std::atan2(sn, dq.w);
            float       rate  = angle * inv;
            if (rate > kMaxAngular) rate = kMaxAngular;
            const float scale = sn > 1.0e-7f ? rate / sn : 0.0f;
            v.w[0] = dq.x * scale, v.w[1] = dq.y * scale, v.w[2] = dq.z * scale;
        }
        t        = 0.0f;
        blending = true;
    }

    bool DeadBlend::Step(Bone* pose, int n, int playing, const DeadBlendTuning& s, BlendFault* fault)
    {
        if (bones != n) {
            Reset(n, playing);
            Record(pose);
            return false;
        }
        if (child != playing && history >= 2 && s.blendTime > 0.0f) Start();
        child = playing;
        if (blending) {
            t += dt;
            if (t >= s.blendTime) {
                blending = false;
            } else {
                const float u     = t / s.blendTime;
                const float alpha = u * u * (3.0f - 2.0f * u);
                const float k     = Coast(t, s.decay);
                for (int i = 0; i < bones; i++) {
                    const Bone in   = pose[i];
                    Bone       live = in;
                    Bone       src  = from[i];
                    for (int a = 0; a < 3; a++) src.t[a] += vel[i].t[a] * k;
                    Quat dq = FromScaledAxis(vel[i].w[0] * k, vel[i].w[1] * k, vel[i].w[2] * k);
                    src.q   = Mul(dq, src.q);
                    for (int a = 0; a < 3; a++) live.t[a] = src.t[a] + (live.t[a] - src.t[a]) * alpha;
                    live.q = Slerp(src.q, live.q, alpha);
                    /// @note The output guard: a non-finite or flung blended bone falls back to the live source's.
                    const float off = std::sqrt((live.t[0] - in.t[0]) * (live.t[0] - in.t[0]) + (live.t[1] - in.t[1]) * (live.t[1] - in.t[1]) +
                                                (live.t[2] - in.t[2]) * (live.t[2] - in.t[2]));
                    if (!FiniteBone(live) || !(off <= kMaxBlendOffset)) {
                        if (fault && fault->bone < 0 && FiniteBone(in)) *fault = BlendFault{ i, live, in, off };
                        live = in;
                    }
                    pose[i] = live;
                }
            }
        }
        Record(pose);
        return true;
    }

}  // namespace CB::engine::locomotion
