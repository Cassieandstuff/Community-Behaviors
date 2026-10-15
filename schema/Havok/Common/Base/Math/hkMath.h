/// @file
/// @brief The SSE vector layer: hkSimdReal, hkVector4, hkQuaternion, hkQsTransform and the scalar helpers.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    typedef __m128 hkQuadReal;

    /// @brief A scalar held broadcast in all four lanes.
    struct hkSimdReal
    {
        hkQuadReal m_real;
        HK_FORCE_INLINE hkSimdReal() {}
        HK_FORCE_INLINE explicit hkSimdReal(hkQuadReal q) : m_real(q) {}
        HK_FORCE_INLINE hkSimdReal(float f) : m_real(_mm_set1_ps(f)) {}
    };

    struct hkQuaternion;

    struct hkVector4
    {
        hkQuadReal m_quad;

        HK_FORCE_INLINE void operator=(const hkVector4& v) { m_quad = v.m_quad; }
        HK_FORCE_INLINE void setZero4() { m_quad = _mm_setzero_ps(); }
        HK_FORCE_INLINE void setAll(float a) { m_quad = _mm_set1_ps(a); }

        HK_FORCE_INLINE hkSimdReal getSimdAt3() const { return hkSimdReal(_mm_shuffle_ps(m_quad, m_quad, _MM_SHUFFLE(3, 3, 3, 3))); }
        HK_FORCE_INLINE float      getW() const { return _mm_cvtss_f32(_mm_shuffle_ps(m_quad, m_quad, _MM_SHUFFLE(3, 3, 3, 3))); }

        HK_FORCE_INLINE void setMul4(const hkVector4& a, const hkVector4& b) { m_quad = _mm_mul_ps(a.m_quad, b.m_quad); }
        HK_FORCE_INLINE void setMul4(const hkVector4& a, hkSimdReal s) { m_quad = _mm_mul_ps(a.m_quad, s.m_real); }
        HK_FORCE_INLINE void mul4(hkSimdReal s) { m_quad = _mm_mul_ps(m_quad, s.m_real); }
        HK_FORCE_INLINE void add4(const hkVector4& a) { m_quad = _mm_add_ps(m_quad, a.m_quad); }
        HK_FORCE_INLINE void sub4(hkSimdReal s) { m_quad = _mm_sub_ps(m_quad, s.m_real); }
        HK_FORCE_INLINE void setAdd4(const hkVector4& a, const hkVector4& b) { m_quad = _mm_add_ps(a.m_quad, b.m_quad); }
        HK_FORCE_INLINE void addMul4(const hkVector4& a, const hkVector4& b) { m_quad = _mm_add_ps(m_quad, _mm_mul_ps(a.m_quad, b.m_quad)); }
        HK_FORCE_INLINE void addMul4(hkSimdReal s, const hkVector4& a) { m_quad = _mm_add_ps(m_quad, _mm_mul_ps(s.m_real, a.m_quad)); }
        HK_FORCE_INLINE void addMul4(const hkVector4& a, hkSimdReal s) { m_quad = _mm_add_ps(m_quad, _mm_mul_ps(a.m_quad, s.m_real)); }

        /// @note Shuffle constants 0xc9 / 0xd2 in this operand order are what vanilla's cross product compiles to.
        HK_FORCE_INLINE void setCross(const hkVector4& a, const hkVector4& b)
        {
            const hkQuadReal cross0 = _mm_mul_ps(_mm_shuffle_ps(a.m_quad, a.m_quad, _MM_SHUFFLE(3, 0, 2, 1)), _mm_shuffle_ps(b.m_quad, b.m_quad, _MM_SHUFFLE(3, 1, 0, 2)));
            const hkQuadReal cross1 = _mm_mul_ps(_mm_shuffle_ps(a.m_quad, a.m_quad, _MM_SHUFFLE(3, 1, 0, 2)), _mm_shuffle_ps(b.m_quad, b.m_quad, _MM_SHUFFLE(3, 0, 2, 1)));
            m_quad = _mm_sub_ps(cross0, cross1);
        }

        HK_FORCE_INLINE hkSimdReal dot3(const hkVector4& a) const
        {
            const hkQuadReal x2  = _mm_mul_ps(m_quad, a.m_quad);
            const hkQuadReal xy  = _mm_add_ss(_mm_shuffle_ps(x2, x2, _MM_SHUFFLE(1, 1, 1, 1)), x2);
            const hkQuadReal xyz = _mm_add_ss(_mm_shuffle_ps(x2, x2, _MM_SHUFFLE(2, 2, 2, 2)), xy);
            return hkSimdReal(_mm_shuffle_ps(xyz, xyz, _MM_SHUFFLE(0, 0, 0, 0)));
        }

        /// @brief xyz from `xyz`, w from lane 3 of `w`.
        HK_FORCE_INLINE void setXYZ_W(const hkVector4& xyz, const hkVector4& w)
        {
            m_quad = _mm_shuffle_ps(xyz.m_quad, _mm_unpackhi_ps(xyz.m_quad, w.m_quad), _MM_SHUFFLE(3, 0, 1, 0));
        }

        HK_FORCE_INLINE void setRotatedDir(const hkQuaternion& quat, const hkVector4& direction);
    };

    extern const hkVector4 g_vectorConstant0001;        ///< (0,0,0,1) — Havok's vector-constant table
    extern const hkVector4 g_vectorConstantMinusHalf;   ///< -0.5 x4
    extern const hkVector4 g_vectorConstantZero;        ///< 0 x4
    extern const hkVector4 g_vectorConstantMax;         ///< HK_REAL_MAX (0x7f7fffee) x4

    struct hkQuaternion
    {
        hkVector4 m_vec;

        HK_FORCE_INLINE void  operator=(const hkQuaternion& q) { m_vec = q.m_vec; }
        HK_FORCE_INLINE float getReal() const { return m_vec.getW(); }
        HK_FORCE_INLINE void  setIdentity() { m_vec = g_vectorConstant0001; }

        HK_FORCE_INLINE void setMul(const hkQuaternion& q0, const hkQuaternion& q1)
        {
            hkVector4 vec;
            vec.setCross(q0.m_vec, q1.m_vec);
            vec.addMul4(q1.m_vec, hkSimdReal(q0.getReal()));
            vec.addMul4(q0.m_vec, hkSimdReal(q1.getReal()));
            hkVector4 w;
            w.setMul4(q0.m_vec, q1.m_vec);
            w.sub4(q0.m_vec.dot3(q1.m_vec));
            m_vec.setXYZ_W(vec, w);
        }
    };

    HK_FORCE_INLINE void hkVector4::setRotatedDir(const hkQuaternion& quat, const hkVector4& direction)
    {
        const hkSimdReal qreal = quat.m_vec.getSimdAt3();
        const hkSimdReal q2minus1(_mm_add_ps(_mm_mul_ps(qreal.m_real, qreal.m_real), g_vectorConstantMinusHalf.m_quad));
        hkVector4 ret;
        ret.setMul4(direction, q2minus1);
        const hkSimdReal imagDotDir = quat.m_vec.dot3(direction);
        ret.addMul4(imagDotDir, quat.m_vec);
        hkVector4 imagCrossDir;
        imagCrossDir.setCross(quat.m_vec, direction);
        ret.addMul4(imagCrossDir, qreal);
        setAdd4(ret, ret);
    }

    struct hkQsTransform
    {
        hkVector4    m_translation;
        hkQuaternion m_rotation;
        hkVector4    m_scale;

        enum IdentityInitializer { IDENTITY };
        HK_FORCE_INLINE hkQsTransform() {}
        HK_FORCE_INLINE explicit hkQsTransform(IdentityInitializer) { setIdentity(); }

        /// @note The scale's (1,1,1,1) is a compiler literal (`_mm_set1_ps`), not Havok's constant table: vanilla loads
        ///       it from the compiler's literal pool while the rotation comes from g_vectorConstant0001.
        HK_FORCE_INLINE void setIdentity()
        {
            m_translation.setZero4();
            m_rotation.setIdentity();
            m_scale.setAll(1.0f);
        }

        HK_FORCE_INLINE void operator=(const hkQsTransform& t)
        {
            m_translation = t.m_translation;
            m_rotation    = t.m_rotation;
            m_scale       = t.m_scale;
        }

        /// @note Built in a local (`rotated`), as vanilla does.
        HK_FORCE_INLINE void setMul(const hkQsTransform& t1, const hkQsTransform& t2)
        {
            hkVector4 rotated;
            rotated.setRotatedDir(t1.m_rotation, t2.m_translation);
            m_translation.setAdd4(rotated, t1.m_translation);
            m_rotation.setMul(t1.m_rotation, t2.m_rotation);
            m_scale.setMul4(t1.m_scale, t2.m_scale);
        }
    };

    struct hkMath
    {
        /// @note `fabsf` compiles to a call under VS2015; the intrinsic double `fabs` narrowed back is what vanilla has.
        static HK_FORCE_INLINE float fabs(float r) { return static_cast<float>(::fabs(r)); }
        /// @note Clamps written through these give maxss/minss; the same ternaries as separate statements branch.
        template <class T> static HK_FORCE_INLINE T max2(T a, T b) { return (a > b) ? a : b; }
        template <class T> static HK_FORCE_INLINE T min2(T a, T b) { return (a < b) ? a : b; }
    };
} }
