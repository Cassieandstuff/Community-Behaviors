#pragma once

#include <algorithm>
#include <cmath>

/// @file
/// @brief The IK solver's own vector and quaternion math: plain floats, no Havok or CommonLib types, so the solver
///        builds and runs anywhere (an offline check included). Callers convert at the boundary.

namespace CB::platform::ik {

    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;

        friend Vec3  operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        friend Vec3  operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        friend Vec3  operator-(Vec3 a) { return { -a.x, -a.y, -a.z }; }
        friend Vec3  operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
        friend Vec3  operator*(float s, Vec3 a) { return a * s; }
        Vec3&        operator+=(Vec3 b) { return *this = *this + b; }
        Vec3&        operator-=(Vec3 b) { return *this = *this - b; }
    };

    inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3  Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }

    /// @return `a` scaled to unit length, or `fallback` when `a` is shorter than `epsilon`.
    inline Vec3 Normalized(Vec3 a, Vec3 fallback = {}, float epsilon = 1e-6f)
    {
        const float len = Length(a);
        return len > epsilon ? a * (1.0f / len) : fallback;
    }

    /// @note (x, y, z) the vector part, w the scalar part, as Havok's hkQuaternion.
    struct Quat {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

        friend Quat operator*(Quat a, Quat b)
        {
            return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
        }
    };

    inline Quat Conjugate(Quat q) { return { -q.x, -q.y, -q.z, q.w }; }

    inline Quat Normalized(Quat q)
    {
        const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        return len > 1e-12f ? Quat{ q.x / len, q.y / len, q.z / len, q.w / len } : Quat{};
    }

    /// @pre `axis` is unit length.
    inline Quat AxisAngle(Vec3 axis, float radians)
    {
        const float s = std::sin(radians * 0.5f);
        return { axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f) };
    }

    inline Vec3 Rotate(Quat q, Vec3 v)
    {
        const Vec3 u{ q.x, q.y, q.z };
        const Vec3 t = 2.0f * Cross(u, v);
        return v + q.w * t + Cross(u, t);
    }

    /// @brief The shortest rotation taking direction `from` onto direction `to`.
    /// @note  Opposite directions turn half a circle about any axis perpendicular to `from`.
    inline Quat ShortestArc(Vec3 from, Vec3 to)
    {
        from          = Normalized(from);
        to            = Normalized(to);
        const float d = std::clamp(Dot(from, to), -1.0f, 1.0f);
        if (d > 1.0f - 1e-6f) return {};
        if (d < -1.0f + 1e-6f) {
            Vec3 axis = Cross(from, { 1.0f, 0.0f, 0.0f });
            if (Dot(axis, axis) < 1e-6f) axis = Cross(from, { 0.0f, 1.0f, 0.0f });
            return AxisAngle(Normalized(axis), 3.14159265f);
        }
        const Vec3 c = Cross(from, to);
        return Normalized(Quat{ c.x, c.y, c.z, 1.0f + d });
    }

}  // namespace CB::platform::ik
