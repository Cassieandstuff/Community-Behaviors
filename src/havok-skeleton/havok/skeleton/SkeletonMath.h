#pragma once
// Transform math for the schema-native skeleton physics compile: quaternion algebra, QSTransform
// composition, FK world poses, and QSTransform → hkTransform columns. Ported verbatim from havok-core's
// sct/SkeletonMath.h (single source of truth for the derivations), but havok-core-FREE: an hkTransform
// is just its 4 Vector4 columns (std::array), built straight into the schema hkTransform.data field.
// Header-only, TU-local to the skeleton compiler.

#include "havok/core/HkTypes.h"   // Vector4 / Quaternion / QSTransform (havok-framing)

#include <array>
#include <cmath>
#include <vector>

#include "havok/skeleton/SkeletonData.h"   // SkeletonData (worldPoses input)

namespace havok::skeleton::skmath {

// 4 Vector4 columns (rotation cols 0..2 + translation) — the plain stand-in for hkTransform.
using Cols4 = std::array<Vector4, 4>;

inline float   vdot(const Vector4& a, const Vector4& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vector4 vsub(const Vector4& a, const Vector4& b) { return Vector4{a.x-b.x, a.y-b.y, a.z-b.z, 0}; }
inline Vector4 vcross(const Vector4& a, const Vector4& b) { return Vector4{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x, 0}; }
inline Vector4 vnorm(const Vector4& v) { float m=std::sqrt(vdot(v,v)); return m>1e-9f? Vector4{v.x/m,v.y/m,v.z/m,0}:Vector4{0,0,1,0}; }

// Hamilton product (x,y,z,w).
inline Quaternion qmul(const Quaternion& a, const Quaternion& b) {
    return Quaternion{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

// Rotate a vector by a unit quaternion.
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

// Compose two QsTransforms: world = parent applied to childLocal (scale, then rotate, then translate).
inline QSTransform compose(const QSTransform& p, const QSTransform& c) {
    const Vector4 sc{ c.translation.x * p.scale.x, c.translation.y * p.scale.y, c.translation.z * p.scale.z, 0.f };
    const Vector4 rt = qrot(p.rotation, sc);
    QSTransform r;
    r.translation = Vector4{ p.translation.x + rt.x, p.translation.y + rt.y, p.translation.z + rt.z, 0.f };
    r.rotation    = qmul(p.rotation, c.rotation);
    r.scale       = Vector4{ p.scale.x * c.scale.x, p.scale.y * c.scale.y, p.scale.z * c.scale.z, 0.f };
    return r;
}

// Inverse of a QsTransform (assumes ~unit scale; ragdoll bind poses are ~unit-scale).
inline QSTransform inverse(const QSTransform& t) {
    QSTransform r;
    r.rotation = qconj(t.rotation);
    const Vector4 negT{ -t.translation.x, -t.translation.y, -t.translation.z, 0.f };
    const Vector4 rt = qrot(r.rotation, negT);
    r.scale       = Vector4{ 1.f / t.scale.x, 1.f / t.scale.y, 1.f / t.scale.z, 0.f };
    r.translation = Vector4{ rt.x * r.scale.x, rt.y * r.scale.y, rt.z * r.scale.z, 0.f };
    return r;
}

// QsTransform → 4 hkTransform columns (rotation cols 0..2 + translation). Column-major (hkRotation).
inline Cols4 toCols(const QSTransform& t) {
    const float x = t.rotation.x, y = t.rotation.y, z = t.rotation.z, w = t.rotation.w;
    return Cols4{
        Vector4{ 1.f - 2.f * (y*y + z*z), 2.f * (x*y + z*w),       2.f * (x*z - y*w),       0.f },
        Vector4{ 2.f * (x*y - z*w),       1.f - 2.f * (x*x + z*z), 2.f * (y*z + x*w),       0.f },
        Vector4{ 2.f * (x*z + y*w),       2.f * (y*z - x*w),       1.f - 2.f * (x*x + y*y), 0.f },
        Vector4{ t.translation.x, t.translation.y, t.translation.z, 0.f } };
}

// FK: each bone's WORLD bind pose from the local reference-pose chain (parent precedes children).
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

} // namespace havok::skeleton::skmath
