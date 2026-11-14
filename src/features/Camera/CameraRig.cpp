#include "PCH.h"

#include "CameraRig.h"

#include <algorithm>

namespace CB::feature::camera::rig {

    void StepOrientation(const OrientationParams& p, OrientationState& s, const OrientationIn& in)
    {
        constexpr float kTwoPi = 6.2831853f;
        s.yaw                  = std::fmod(s.yaw + in.lookX * p.speed * in.dt, kTwoPi);
        if (s.yaw < 0.0f) s.yaw += kTwoPi;
        s.pitch = std::clamp(s.pitch - in.lookY * p.speed * in.dt, -p.maxPitch, p.maxPitch);
    }

    Vec3 StepAnchor(const AnchorParams& p, AnchorState& s, const AnchorIn& in)
    {
        if (!s.init) {
            s.init = true;
            s.pos  = in.target;
            return s.pos;
        }
        s.pos.x = Damp(s.pos.x, in.target.x, p.halflifeXY, in.dt);
        s.pos.y = Damp(s.pos.y, in.target.y, p.halflifeXY, in.dt);
        s.pos.z = Damp(s.pos.z, in.target.z, p.halflifeZ, in.dt);
        return s.pos;
    }

    float BoomDistance(const BoomParams& p, const BoomIn& in) { return p.minDistance + (p.maxDistance - p.minDistance) * in.zoom + in.pitchZoom; }

    BoomOut StepBoom(const BoomParams& p, const BoomIn& in)
    {
        const float distance = in.distance >= 0.0f ? in.distance : BoomDistance(p, in);
        BoomOut     out;
        out.pivot = in.anchor + in.rotation.Apply({ in.shoulder.x, 0.0f, in.shoulder.z });
        out.eye   = in.anchor + in.rotation.Apply({ in.shoulder.x, in.shoulder.y - distance, in.shoulder.z });
        return out;
    }

    FramingOut StepFraming(const FramingParams& p, const FramingIn& in)
    {
        FramingOut out;
        out.centre = in.anchor;
        Vec3 sum;
        for (const FramePoint& f : in.points) {
            if (f.weight <= 0.0f) continue;
            sum        = sum + f.pos * f.weight;
            out.weight += f.weight;
        }
        if (out.weight <= 0.0f) return out;
        out.centre = sum * (1.0f / out.weight);

        out.shift         = (out.centre - in.anchor) * (out.weight / (out.weight + p.playerWeight));
        const float shift = Length(out.shift);
        if (shift > p.maxShift) out.shift = out.shift * (p.maxShift / shift);
        const Vec3 pivot = in.anchor + out.shift;

        /// @note Into the camera's frame: world = M * local, so local = transpose(M) * world (the columns).
        const auto& m    = in.rotation.m;
        const auto  need = [&](Vec3 at, float radius) {
            const Vec3  d  = at - pivot;
            const float lx = m[0][0] * d.x + m[1][0] * d.y + m[2][0] * d.z - in.shoulder.x;
            const float ly = m[0][1] * d.x + m[1][1] * d.y + m[2][1] * d.z;
            const float lz = m[0][2] * d.x + m[1][2] * d.y + m[2][2] * d.z - in.shoulder.z;
            const float h  = (std::abs(lx) + radius) / (in.tanHalfH * p.margin);
            const float v  = (std::abs(lz) + radius) / (in.tanHalfV * p.margin);
            return (h > v ? h : v) - ly;
        };
        float distance = need(in.anchor, p.playerRadius);
        for (const FramePoint& f : in.points)
            if (f.weight > 0.0f) {
                const float d = need(f.pos, f.radius) * f.weight;
                if (d > distance) distance = d;
            }
        out.distance = distance > 0.0f ? distance : 0.0f;
        return out;
    }

    Vec3 StepProbes(const ProbeParams& p, ProbeState& s, const ProbeIn& in)
    {
        const auto push = [](float fraction) {
            const float close = 1.0f - std::clamp(fraction, 0.0f, 1.0f);
            return close * close;
        };
        const float x = StepEase(p.spring, s.x, (push(in.left) - push(in.right)) * p.maxShift, in.dt);
        const float z = StepEase(p.spring, s.z, (push(in.down) - push(in.up)) * p.maxShift, in.dt);
        return { x, 0.0f, z };
    }

    float SoftPull(const ProbeParams& p, float beyond)
    {
        if (p.reach <= 0.0f) return 0.0f;
        const float close = 1.0f - std::clamp(beyond / p.reach, 0.0f, 1.0f);
        return close * close * p.boomPull;
    }

    float StepEase(const EaseParams& p, EaseState& s, float target, float dt)
    {
        if (p.settle <= 0.0f) s = { target, 0.0f };
        if (p.settle <= 0.0f || dt <= 0.0f) return s.value;
        const float omega  = 2.0f / p.settle;
        const float x      = omega * dt;
        const float decay  = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        const float change = s.value - target;
        const float temp   = (s.rate + omega * change) * dt;
        s.rate             = (s.rate - omega * temp) * decay;
        s.value            = target + (change + temp) * decay;
        return s.value;
    }

    Vec3 StepCollision(const CollisionParams& p, CollisionState& s, const CollisionIn& in)
    {
        const Vec3  arm  = in.eye - in.pivot;
        const float full = Length(arm);
        if (full <= 1.0e-3f) return in.eye;
        const bool  hit   = in.hitFraction < 1.0f;
        const float clear = hit ? in.hitFraction * full : (std::max)(0.0f, full - in.softPull);
        if (!s.init) {
            s.init     = true;
            s.distance = clear;
        } else if (clear <= s.distance) {
            if (!hit) s.distance = Damp(s.distance, clear, p.softInHalflife, in.dt);
            else      s.distance = in.thin ? Damp(s.distance, clear, p.thinInHalflife, in.dt) : clear;
            if (hit) s.hold = p.holdTime;
        } else if (s.hold > 0.0f) {
            s.hold -= in.dt;
        } else {
            s.distance = Damp(s.distance, clear, p.recoverHalflife, in.dt);
        }
        return in.pivot + arm * (s.distance / full);
    }

}  // namespace CB::feature::camera::rig
