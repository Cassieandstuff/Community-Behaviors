#pragma once
// Internal transform math for the skeleton physics compile (Stage 4/5): quaternion algebra, QSTransform
// composition, FK world poses, and QSTransform -> hkTransform. The physics derivations (body transforms,
// constraint frames, mapper transforms, capsule endpoints) all build on these. Header-only, TU-local.

#include "havok/core/HkTypes.h"
#include "havok/classes/Physics.h"      // hkTransform (defined there)
#include "havok/sct/SkeletonImport.h"   // SkeletonData

#include <vector>

namespace havok::sct::skmath {

// Hamilton product (x,y,z,w).
inline Quaternion qmul(const Quaternion& a, const Quaternion& b) {
    return Quaternion{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

// Rotate a vector by a unit quaternion: v' = v + 2w(u×v) + 2(u×(u×v)), u = (x,y,z).
inline Vector4 qrot(const Quaternion& q, const Vector4& v) {
    const float tx = 2.f * (q.y * v.z - q.z * v.y);
    const float ty = 2.f * (q.z * v.x - q.x * v.z);
    const float tz = 2.f * (q.x * v.y - q.y * v.x);
    return Vector4{
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx),
        0.f };
}

inline Quaternion qconj(const Quaternion& q) { return Quaternion{ -q.x, -q.y, -q.z, q.w }; }

// Compose two QsTransforms: `world` = `parent` applied to `childLocal` (TRS: scale, then rotate, then
// translate). Matches Havok's hkQsTransform::setMul(parent, child) / Pose::SolveFK accumulation.
inline QSTransform compose(const QSTransform& p, const QSTransform& c) {
    const Vector4 sc{ c.translation.x * p.scale.x, c.translation.y * p.scale.y, c.translation.z * p.scale.z, 0.f };
    const Vector4 rt = qrot(p.rotation, sc);
    QSTransform r;
    r.translation = Vector4{ p.translation.x + rt.x, p.translation.y + rt.y, p.translation.z + rt.z, 0.f };
    r.rotation    = qmul(p.rotation, c.rotation);
    r.scale       = Vector4{ p.scale.x * c.scale.x, p.scale.y * c.scale.y, p.scale.z * c.scale.z, 0.f };
    return r;
}

// Inverse of a QsTransform (assumes uniform-ish scale; ragdoll bind poses are ~unit-scale).
inline QSTransform inverse(const QSTransform& t) {
    QSTransform r;
    r.rotation = qconj(t.rotation);
    const Vector4 negT{ -t.translation.x, -t.translation.y, -t.translation.z, 0.f };
    const Vector4 rt = qrot(r.rotation, negT);
    r.scale       = Vector4{ 1.f / t.scale.x, 1.f / t.scale.y, 1.f / t.scale.z, 0.f };
    r.translation = Vector4{ rt.x * r.scale.x, rt.y * r.scale.y, rt.z * r.scale.z, 0.f };
    return r;
}

// QsTransform -> hkTransform (4 Vector4: rotation columns 0..2 + translation). Column-major to match
// Havok's hkRotation (m_data[col]).
inline hkTransform toHkTransform(const QSTransform& t) {
    const float x = t.rotation.x, y = t.rotation.y, z = t.rotation.z, w = t.rotation.w;
    hkTransform h;
    h.m_data[0] = Vector4{ 1.f - 2.f * (y*y + z*z), 2.f * (x*y + z*w),       2.f * (x*z - y*w),       0.f };
    h.m_data[1] = Vector4{ 2.f * (x*y - z*w),       1.f - 2.f * (x*x + z*z), 2.f * (y*z + x*w),       0.f };
    h.m_data[2] = Vector4{ 2.f * (x*z + y*w),       2.f * (y*z - x*w),       1.f - 2.f * (x*x + y*y), 0.f };
    h.m_data[3] = Vector4{ t.translation.x, t.translation.y, t.translation.z, 0.f };
    return h;
}

// FK: each bone's WORLD bind pose from the local reference-pose chain. Index assignment guarantees a
// parent precedes its children, so one forward pass suffices.
inline std::vector<QSTransform> worldPoses(const SkeletonData& d) {
    std::vector<QSTransform> w(d.bones.size());
    for (std::size_t i = 0; i < d.bones.size(); ++i) {
        const auto& b = d.bones[i];
        w[i] = (b.parentIndex >= 0 && b.parentIndex < static_cast<int>(i))
                   ? compose(w[static_cast<std::size_t>(b.parentIndex)], b.refPose)
                   : b.refPose;
    }
    return w;
}

} // namespace havok::sct::skmath
