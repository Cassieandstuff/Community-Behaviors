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

    BoomOut StepBoom(const BoomParams& p, const BoomIn& in)
    {
        const float distance = p.minDistance + (p.maxDistance - p.minDistance) * in.zoom + in.pitchZoom;
        BoomOut     out;
        out.pivot = in.anchor + in.rotation.Apply({ in.shoulder.x, 0.0f, in.shoulder.z });
        out.eye   = in.anchor + in.rotation.Apply({ in.shoulder.x, in.shoulder.y - distance, in.shoulder.z });
        return out;
    }

    Vec3 StepCollision(const CollisionParams& p, CollisionState& s, const CollisionIn& in)
    {
        const Vec3  arm  = in.eye - in.pivot;
        const float full = Length(arm);
        if (full <= 1.0e-3f) return in.eye;
        const float clear = in.hitFraction < 1.0f ? (std::max)(0.0f, in.hitFraction * full - p.radius) : full;
        if (!s.init || clear <= s.distance) {
            s.init     = true;
            s.distance = clear;
        } else {
            s.distance = Damp(s.distance, clear, p.recoverHalflife, in.dt);
        }
        return in.pivot + arm * (s.distance / full);
    }

}  // namespace CB::feature::camera::rig
