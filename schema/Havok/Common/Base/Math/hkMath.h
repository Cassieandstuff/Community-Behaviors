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

        /// @note Through memory (`_mm_store_ss`), as vanilla converts it: a store and reload before every compare.
        HK_FORCE_INLINE hkReal getReal() const
        {
            hkReal r;
            _mm_store_ss(&r, m_real);
            return r;
        }
    };

    struct hkQuaternion;
    struct hkQsTransform;
    struct hkTransform;
    struct hkRotation;

    struct hkVector4
    {
        hkQuadReal m_quad;

        HK_FORCE_INLINE hkVector4() {}
        /// @note User-defined, as Havok's: a copied hkQsTransform member then moves as aligned vectors (movaps), not as
        ///       an unaligned block copy (movups).
        HK_FORCE_INLINE hkVector4(const hkVector4& v) : m_quad(v.m_quad) {}
        HK_FORCE_INLINE hkVector4(hkReal x, hkReal y, hkReal z, hkReal w = 0.0f) : m_quad(_mm_setr_ps(x, y, z, w)) {}
        HK_FORCE_INLINE explicit hkVector4(hkQuadReal q) : m_quad(q) {}
        HK_FORCE_INLINE void operator=(const hkVector4& v) { m_quad = v.m_quad; }
        HK_FORCE_INLINE void setZero4() { m_quad = _mm_setzero_ps(); }
        /// @note A load from g_vectorConstantZero, where setZero4 is an xorps (the node default constructors read it).
        static HK_FORCE_INLINE const hkVector4& getZero();
        HK_FORCE_INLINE void setAll(float a) { m_quad = _mm_set1_ps(a); }
        /// @brief Component `i`, through memory.
        HK_FORCE_INLINE hkReal& operator()(int i) { return m_quad.m128_f32[i]; }
        HK_FORCE_INLINE const hkReal& operator()(int i) const { return m_quad.m128_f32[i]; }
        HK_FORCE_INLINE void set(hkReal x, hkReal y, hkReal z, hkReal w = 0.0f) { m_quad = _mm_setr_ps(x, y, z, w); }

        HK_FORCE_INLINE hkSimdReal getSimdAt3() const { return hkSimdReal(_mm_shuffle_ps(m_quad, m_quad, _MM_SHUFFLE(3, 3, 3, 3))); }
        HK_FORCE_INLINE float      getW() const { return _mm_cvtss_f32(_mm_shuffle_ps(m_quad, m_quad, _MM_SHUFFLE(3, 3, 3, 3))); }

        HK_FORCE_INLINE void setMul4(const hkVector4& a, const hkVector4& b) { m_quad = _mm_mul_ps(a.m_quad, b.m_quad); }
        HK_FORCE_INLINE void setMul4(const hkVector4& a, hkSimdReal s) { m_quad = _mm_mul_ps(a.m_quad, s.m_real); }
        HK_FORCE_INLINE void mul4(hkSimdReal s) { m_quad = _mm_mul_ps(m_quad, s.m_real); }
        HK_FORCE_INLINE void add4(const hkVector4& a) { m_quad = _mm_add_ps(m_quad, a.m_quad); }
        HK_FORCE_INLINE void sub4(hkSimdReal s) { m_quad = _mm_sub_ps(m_quad, s.m_real); }
        HK_FORCE_INLINE void setSub4(const hkVector4& a, const hkVector4& b) { m_quad = _mm_sub_ps(a.m_quad, b.m_quad); }
        HK_FORCE_INLINE void setAdd4(const hkVector4& a, const hkVector4& b) { m_quad = _mm_add_ps(a.m_quad, b.m_quad); }
        HK_FORCE_INLINE void addMul4(const hkVector4& a, const hkVector4& b) { m_quad = _mm_add_ps(m_quad, _mm_mul_ps(a.m_quad, b.m_quad)); }
        HK_FORCE_INLINE void addMul4(hkSimdReal s, const hkVector4& a) { m_quad = _mm_add_ps(m_quad, _mm_mul_ps(s.m_real, a.m_quad)); }
        HK_FORCE_INLINE void addMul4(const hkVector4& a, hkSimdReal s) { m_quad = _mm_add_ps(m_quad, _mm_mul_ps(a.m_quad, s.m_real)); }
        HK_FORCE_INLINE void subMul4(hkSimdReal s, const hkVector4& a) { m_quad = _mm_sub_ps(m_quad, _mm_mul_ps(s.m_real, a.m_quad)); }
        HK_FORCE_INLINE void subMul4(const hkVector4& a, hkSimdReal s) { m_quad = _mm_sub_ps(m_quad, _mm_mul_ps(a.m_quad, s.m_real)); }
        HK_FORCE_INLINE void setMul4(hkSimdReal s, const hkVector4& a) { m_quad = _mm_mul_ps(s.m_real, a.m_quad); }

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

        /// @brief The four-lane dot product, in every lane.
        HK_FORCE_INLINE hkSimdReal dot4(const hkVector4& a) const
        {
            const hkQuadReal x2   = _mm_mul_ps(m_quad, a.m_quad);
            const hkQuadReal sum2 = _mm_add_ps(_mm_shuffle_ps(x2, x2, _MM_SHUFFLE(1, 0, 3, 2)), x2);
            return hkSimdReal(_mm_add_ps(_mm_shuffle_ps(sum2, sum2, _MM_SHUFFLE(2, 3, 0, 1)), sum2));
        }
        HK_FORCE_INLINE hkSimdReal lengthSquared3() const { return dot3(*this); }
        /// @note sqrtss on the broadcast squared length (lane 0 is the result).
        HK_FORCE_INLINE hkSimdReal length3() const { return hkSimdReal(_mm_sqrt_ss(lengthSquared3().m_real)); }
        HK_FORCE_INLINE hkSimdReal lengthSquared4() const { return dot4(*this); }
        /// @brief Scales to unit length (rsqrtps plus one Newton-Raphson step) unless the xyz length is zero.
        HK_FORCE_INLINE void normalize3IfNotZero();

        /// @brief (1 − t)·a + t·b.
        HK_FORCE_INLINE void setInterpolate4(const hkVector4& a, const hkVector4& b, hkSimdReal t)
        {
            const hkQuadReal t4        = _mm_shuffle_ps(t.m_real, t.m_real, _MM_SHUFFLE(0, 0, 0, 0));
            const hkQuadReal oneMinusT = _mm_sub_ps(_mm_set1_ps(1.0f), t4);
            m_quad                     = _mm_add_ps(_mm_mul_ps(oneMinusT, a.m_quad), _mm_mul_ps(t4, b.m_quad));
        }

        /// @brief Scales to unit length; a zero vector stays zero.
        /// @note One Newton step on the scalar rsqrt estimate.
        HK_FORCE_INLINE void normalize4()
        {
            const hkQuadReal d       = dot4(*this).m_real;
            const hkQuadReal len2    = _mm_shuffle_ps(d, d, _MM_SHUFFLE(0, 0, 0, 0));
            const hkQuadReal e       = _mm_rsqrt_ss(len2);
            const hkQuadReal isZero  = _mm_cmpeq_ss(len2, _mm_set_ss(0.0f));
            const hkQuadReal invSqrt = _mm_mul_ss(_mm_mul_ss(_mm_set_ss(0.5f), e), _mm_sub_ss(_mm_set_ss(3.0f), _mm_mul_ss(_mm_mul_ss(len2, e), e)));
            const hkQuadReal inv     = _mm_andnot_ps(isZero, invSqrt);
            m_quad                   = _mm_mul_ps(_mm_shuffle_ps(inv, inv, _MM_SHUFFLE(0, 0, 0, 0)), m_quad);
        }

        /// @brief Scales x, y and z (and w) by 1/|xyz|; a zero vector stays zero.
        HK_FORCE_INLINE void normalize3();
        /// @brief normalize3, returning the length it divided by (0 for a zero vector).
        HK_FORCE_INLINE hkSimdReal normalizeWithLength3();

        /// @brief xyz from `xyz`, w from lane 3 of `w`.
        HK_FORCE_INLINE void setXYZ_W(const hkVector4& xyz, const hkVector4& w)
        {
            m_quad = _mm_shuffle_ps(xyz.m_quad, _mm_unpackhi_ps(xyz.m_quad, w.m_quad), _MM_SHUFFLE(3, 0, 1, 0));
        }

        HK_FORCE_INLINE void setRotatedDir(const hkQuaternion& quat, const hkVector4& direction);
        /// @brief Rotates `direction` by the inverse of `quat`.
        HK_FORCE_INLINE void setRotatedInverseDir(const hkQuaternion& quat, const hkVector4& direction);
        /// @note Flips the sign bits (xor with -0 x4).
        HK_FORCE_INLINE void setNeg4(const hkVector4& v) { m_quad = _mm_xor_ps(v.m_quad, _mm_castsi128_ps(_mm_set1_epi32(int(0x80000000)))); }
        /// @brief 1/v for x, y and z (rcpps plus one Newton-Raphson step); w is 0.
        /// @note w is set to 1 before the estimate so it cannot overflow, then masked off.
        HK_FORCE_INLINE void setReciprocal3(const hkVector4& v);
        /// @brief t·v (rotation then translation), and its inverse.
        /// @note Out of line in vanilla (Havok's non-underscore transform family).
        void setTransformedPos(const hkTransform& t, const hkVector4& v);
        void setTransformedInversePos(const hkTransform& t, const hkVector4& v);
        void setTransformedPos(const hkQsTransform& t, const hkVector4& v);
        void setTransformedInversePos(const hkQsTransform& t, const hkVector4& v);

        /// @brief Whether the xyz length is 1 within `epsilon`.
        /// @note Out of line in vanilla (0x140a8d170); returns the hkBool through a hidden pointer.
        hkBool isNormalized3(hkReal epsilon = 1e-4f) const;
        /// @brief Whether x, y and z are all finite.
        /// @note Out of line in vanilla (0x140a8d110); returns the hkBool through a hidden pointer.
        hkBool isOk3() const;

        /// @brief Whether x, y and z each lie within `epsilon` of `v`'s.
        HK_FORCE_INLINE bool equals3(const hkVector4& v, hkReal epsilon) const
        {
            const hkQuadReal absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
            const hkQuadReal diff    = _mm_and_ps(_mm_sub_ps(m_quad, v.m_quad), absMask);
            const hkQuadReal eps     = _mm_set1_ps(epsilon);
            return (_mm_movemask_ps(_mm_cmple_ps(diff, eps)) & 7) == 7;
        }
        /// @brief Whether all four components lie within `epsilon` of `v`'s.
        HK_FORCE_INLINE bool equals4(const hkVector4& v, hkReal epsilon) const
        {
            const hkQuadReal absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
            const hkQuadReal diff    = _mm_and_ps(_mm_sub_ps(m_quad, v.m_quad), absMask);
            const hkQuadReal eps     = _mm_set1_ps(epsilon);
            return (_mm_movemask_ps(_mm_cmple_ps(diff, eps)) & 0xf) == 0xf;
        }
    };

    extern const hkVector4 g_vectorConstant0001;        ///< (0,0,0,1) — Havok's vector-constant table
    extern const hkVector4 g_vectorConstantMinusHalf;   ///< -0.5 x4
    extern const hkVector4 g_vectorConstantZero;        ///< 0 x4
    extern const hkVector4 g_vectorConstantMax;         ///< HK_REAL_MAX (0x7f7fffee) x4
    extern const hkVector4 g_vectorConstantOne;         ///< 1 x4
    /// @brief 0 x4 from the table holding g_vectorConstant0001 (g_vectorConstantZero is another table's zero).
    /// @note Name provisional (which of the two zeros Havok calls what is unknown); hkQsTransform::blendAddMul and
    ///       setInterpolate4 negate through it.
    extern const hkVector4 g_quadRealZero;
    /// @brief (0,0,0,1) from g_vectorConstantZero's table (-1, 0, 1, 2, …, 1000, 0100, 0010, 0001, MAX).
    /// @note Name provisional. Not g_vectorConstant0001 (the g_quadRealZero table's): the node default constructors
    ///       read this one (hkQuaternion::getIdentity), hkQsTransform::setIdentity the other.
    extern const hkVector4 g_vectorConstants0001;
    /// @brief 0.5 x4 and 3 x4 from g_quadRealZero's table (just before it).
    /// @note Names provisional; hkVector4::normalize3IfNotZero's Newton step reads them.
    extern const hkVector4 g_quadRealInv2;
    extern const hkVector4 g_quadReal3;

    HK_FORCE_INLINE const hkVector4& hkVector4::getZero() { return g_vectorConstantZero; }

    /// @note Subtracts from g_quadRealZero (not g_vectorConstantZero) and re-broadcasts lane 0, as vanilla does.
    HK_FORCE_INLINE hkSimdReal operator-(hkSimdReal a)
    {
        const hkQuadReal neg = _mm_sub_ps(g_quadRealZero.m_quad, a.m_real);
        return hkSimdReal(_mm_shuffle_ps(neg, neg, _MM_SHUFFLE(0, 0, 0, 0)));
    }

    /// @brief Scalar arithmetic on broadcast reals.
    /// @note Each result is re-broadcast from lane 0 (`shufps r,r,0` after the packed op), as vanilla's spline
    ///       evaluators (hkaSplineCompressedAnimation::evaluateSimple2/3) compile.
    HK_FORCE_INLINE hkSimdReal operator+(hkSimdReal a, hkSimdReal b)
    {
        const hkQuadReal r = _mm_add_ps(a.m_real, b.m_real);
        return hkSimdReal(_mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0)));
    }
    HK_FORCE_INLINE hkSimdReal operator-(hkSimdReal a, hkSimdReal b)
    {
        const hkQuadReal r = _mm_sub_ps(a.m_real, b.m_real);
        return hkSimdReal(_mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0)));
    }
    HK_FORCE_INLINE hkSimdReal operator*(hkSimdReal a, hkSimdReal b)
    {
        const hkQuadReal r = _mm_mul_ps(a.m_real, b.m_real);
        return hkSimdReal(_mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0)));
    }
    HK_FORCE_INLINE hkSimdReal operator/(hkSimdReal a, hkSimdReal b)
    {
        const hkQuadReal r = _mm_div_ps(a.m_real, b.m_real);
        return hkSimdReal(_mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0)));
    }

    /// @note The zero test is the sign of the negated square (movmskps), as vanilla's.
    HK_FORCE_INLINE void hkVector4::normalize3IfNotZero()
    {
        const hkSimdReal lengthSquared = lengthSquared3();
        if (_mm_movemask_ps((-lengthSquared).m_real) & 1) {
            const hkQuadReal e      = _mm_rsqrt_ps(lengthSquared.m_real);
            const hkQuadReal ree    = _mm_mul_ps(_mm_mul_ps(lengthSquared.m_real, e), e);
            const hkQuadReal invLen = _mm_mul_ps(_mm_sub_ps(g_quadReal3.m_quad, ree), _mm_mul_ps(e, g_quadRealInv2.m_quad));
            m_quad = _mm_mul_ps(_mm_shuffle_ps(invLen, invLen, _MM_SHUFFLE(0, 0, 0, 0)), m_quad);
        }
    }

    struct hkQuaternion
    {
        hkVector4 m_vec;

        HK_FORCE_INLINE void  operator=(const hkQuaternion& q) { m_vec = q.m_vec; }
        HK_FORCE_INLINE float getReal() const { return m_vec.getW(); }
        HK_FORCE_INLINE void  setIdentity() { m_vec = g_vectorConstant0001; }
        static HK_FORCE_INLINE const hkQuaternion& getIdentity() { return reinterpret_cast<const hkQuaternion&>(g_vectorConstants0001); }
        /// @brief The conjugate: x, y and z negated.
        HK_FORCE_INLINE void  setInverse(const hkQuaternion& q) { m_vec.m_quad = _mm_xor_ps(q.m_vec.m_quad, _mm_castsi128_ps(_mm_setr_epi32(int(0x80000000), int(0x80000000), int(0x80000000), 0))); }
        HK_FORCE_INLINE void  normalize() { m_vec.normalize4(); }
        /// @brief The rotation by `angle` radians about the unit vector `axis`.
        /// @note Out of line in vanilla.
        void setAxisAngle(const hkVector4& axis, hkReal angle);
        /// @brief The rotation `r` as a quaternion.
        /// @note Out of line in vanilla (0x140a8a360).
        void set(const hkRotation& r);
        /// @brief Spherical interpolation from `q0` (t = 0) to `q1` (t = 1) on the shorter arc, renormalized; nearly
        ///        parallel inputs blend linearly.
        /// @note Out of line in vanilla (0x140a8ab10); `t` arrives as a float (xmm3).
        void setSlerp(const hkQuaternion& q0, const hkQuaternion& q1, hkReal t);
        /// @brief The shortest-arc rotation taking the unit vector `from` onto the unit vector `to`.
        /// @note Out of line in vanilla: its one copy (0x1403f3120, unaligned loads) is a COMDAT the linker kept from
        ///       Bethesda's code, called by hkbComputeRotationToTargetModifierUtils.
        void setShortestRotation(const hkVector4& from, const hkVector4& to);

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

        /// @brief q0 · q1⁻¹: the rotation taking q1 to q0.
        /// @note w is the four-lane dot product, re-broadcast.
        HK_FORCE_INLINE void setMulInverse(const hkQuaternion& q0, const hkQuaternion& q1)
        {
            hkVector4 vec;
            vec.setCross(q1.m_vec, q0.m_vec);
            vec.subMul4(q1.m_vec, hkSimdReal(q0.getReal()));
            vec.addMul4(q0.m_vec, hkSimdReal(q1.getReal()));
            const hkQuadReal w = q0.m_vec.dot4(q1.m_vec).m_real;
            m_vec.setXYZ_W(vec, hkVector4(_mm_shuffle_ps(w, w, _MM_SHUFFLE(0, 0, 0, 0))));
        }

        /// @brief The rotation angle in radians, in [0, 2pi].
        HK_FORCE_INLINE hkReal getAngle() const;
        /// @brief Whether the axis is long enough to normalize (|xyz| > HK_REAL_EPSILON).
        HK_FORCE_INLINE bool hasValidAxis() const { return m_vec.length3().getReal() > HK_REAL_EPSILON; }
        /// @brief The unit rotation axis, flipped so the angle is in [0, pi] when w is negative.
        /// @pre hasValidAxis().
        HK_FORCE_INLINE void getAxis(hkVector4& axisOut) const
        {
            axisOut = m_vec;
            axisOut.normalize3();
            if (_mm_movemask_ps(_mm_cmplt_ps(m_vec.m_quad, _mm_setzero_ps())) & 8) axisOut.setNeg4(axisOut);
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

    /// @note setRotatedDir with the cross product's operands swapped.
    HK_FORCE_INLINE void hkVector4::setRotatedInverseDir(const hkQuaternion& quat, const hkVector4& direction)
    {
        const hkSimdReal qreal = quat.m_vec.getSimdAt3();
        const hkSimdReal q2minus1(_mm_add_ps(_mm_mul_ps(qreal.m_real, qreal.m_real), g_vectorConstantMinusHalf.m_quad));
        hkVector4 ret;
        ret.setMul4(direction, q2minus1);
        const hkSimdReal imagDotDir = quat.m_vec.dot3(direction);
        ret.addMul4(imagDotDir, quat.m_vec);
        hkVector4 dirCrossImag;
        dirCrossImag.setCross(direction, quat.m_vec);
        ret.addMul4(dirCrossImag, qreal);
        setAdd4(ret, ret);
    }

    HK_FORCE_INLINE void hkVector4::setReciprocal3(const hkVector4& v)
    {
        hkVector4 val;
        val.setXYZ_W(v, g_vectorConstantOne);
        const hkQuadReal r = _mm_rcp_ps(val.m_quad);
        const hkQuadReal refined = _mm_sub_ps(_mm_add_ps(r, r), _mm_mul_ps(_mm_mul_ps(r, val.m_quad), r));
        m_quad = _mm_and_ps(refined, _mm_castsi128_ps(_mm_setr_epi32(-1, -1, -1, 0)));
    }

    /// @brief The identity transform hkQsTransform::getIdentity() reads (writable data, filled at startup).
    extern const hkQuadReal hkQsTransform_identityStorage[3];

    struct hkQsTransform
    {
        hkVector4    m_translation;
        hkQuaternion m_rotation;
        hkVector4    m_scale;

        static HK_FORCE_INLINE const hkQsTransform& getIdentity() { return reinterpret_cast<const hkQsTransform&>(hkQsTransform_identityStorage); }

        enum IdentityInitializer { IDENTITY };
        HK_FORCE_INLINE hkQsTransform() {}
        HK_FORCE_INLINE explicit hkQsTransform(IdentityInitializer) { setIdentity(); }
        /// @note The unit scale is (1,1,1,0), a compiler literal.
        HK_FORCE_INLINE hkQsTransform(const hkVector4& translation, const hkQuaternion& rotation)
        :   m_translation(translation), m_rotation(rotation), m_scale(1.0f, 1.0f, 1.0f) {}

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

        /// @brief Blends `a` to `b` by `t`: translation and scale linearly, rotation by normalized lerp on the nearer
        ///        hemisphere.
        HK_FORCE_INLINE void setInterpolate4(const hkQsTransform& a, const hkQsTransform& b, hkSimdReal t)
        {
            m_scale.setInterpolate4(a.m_scale, b.m_scale, t);
            m_translation.setInterpolate4(a.m_translation, b.m_translation, t);

            const hkSimdReal oneMinusT(_mm_shuffle_ps(_mm_sub_ps(g_vectorConstantOne.m_quad, t.m_real), _mm_sub_ps(g_vectorConstantOne.m_quad, t.m_real), _MM_SHUFFLE(0, 0, 0, 0)));
            hkReal           polarity;
            _mm_store_ss(&polarity, b.m_rotation.m_vec.dot4(a.m_rotation.m_vec).m_real);
            const hkSimdReal signedT = (polarity < 0.0f) ? hkSimdReal(_mm_shuffle_ps(_mm_sub_ps(g_quadRealZero.m_quad, t.m_real), _mm_sub_ps(g_quadRealZero.m_quad, t.m_real), _MM_SHUFFLE(0, 0, 0, 0))) : t;

            hkQuaternion rot;
            rot.m_vec.setMul4(a.m_rotation.m_vec, oneMinusT);
            rot.m_vec.addMul4(b.m_rotation.m_vec, signedT);
            rot.m_vec.normalize4();
            m_rotation = rot;
        }
        /// @note Out of line in vanilla.
        hkBool isApproximatelyEqual(const hkQsTransform& other, hkReal epsilon) const;
        /// @brief The rigid transform `transform` with unit scale.
        /// @note Out of line in vanilla (0x140a8de50).
        void setFromTransformNoScale(const hkTransform& transform);

        HK_FORCE_INLINE void setInverse(const hkQsTransform& t)
        {
            m_rotation.setInverse(t.m_rotation);
            m_translation.setRotatedInverseDir(t.m_rotation, t.m_translation);
            m_translation.setNeg4(m_translation);
            m_scale.setReciprocal3(t.m_scale);
        }

        /// @brief inverse(t1) x t2.
        HK_FORCE_INLINE void setMulInverseMul(const hkQsTransform& t1, const hkQsTransform& t2)
        {
            hkQsTransform inverse;
            inverse.setInverse(t1);
            setMul(inverse, t2);
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

        /// @brief Accumulates `weight` × `other` for a later blendNormalize; the rotation is added on `other`'s
        ///        hemisphere nearest this sum.
        HK_FORCE_INLINE void blendAddMul(const hkQsTransform& other, hkSimdReal weight);
        /// @brief Divides an accumulated blend by `totalWeight` and renormalizes it: a near-zero weight gives the
        ///        identity, a near-zero rotation or scale its identity.
        HK_FORCE_INLINE void blendNormalize(hkSimdReal totalWeight);
    };

    /// @brief A 3x3 rotation as three columns.
    struct hkRotation
    {
        hkVector4 m_col0;
        hkVector4 m_col1;
        hkVector4 m_col2;
    };

    /// @brief A 4x4 matrix as four columns.
    struct hkMatrix4
    {
        hkVector4 m_col0;
        hkVector4 m_col1;
        hkVector4 m_col2;
        hkVector4 m_col3;
    };

    /// @brief A rigid transform: rotation, then translation (+0x30).
    struct hkTransform
    {
        hkRotation m_rotation;
        hkVector4  m_translation;

        HK_FORCE_INLINE const hkVector4& getTranslation() const { return m_translation; }
    };

    struct hkMath
    {
        /// @note `fabsf` compiles to a call under VS2015; the intrinsic double `fabs` narrowed back is what vanilla has.
        static HK_FORCE_INLINE float fabs(float r) { return static_cast<float>(::fabs(r)); }
        /// @brief Hints the cache line at `p` in (prefetchnta).
        static HK_FORCE_INLINE void prefetch128(const void* p) { _mm_prefetch(static_cast<const char*>(p), _MM_HINT_NTA); }
        /// @note Clamps written through these give maxss/minss; the same ternaries as separate statements branch.
        template <class T> static HK_FORCE_INLINE T max2(T a, T b) { return (a > b) ? a : b; }
        template <class T> static HK_FORCE_INLINE T min2(T a, T b) { return (a < b) ? a : b; }

        /// @brief atan2(y, x) by a polynomial approximation (radians).
        /// @note Out of line in vanilla (0x140a917c0).
        static hkReal atan2fApproximation(hkReal y, hkReal x);
        /// @brief asin, clamped to ±pi/2 outside (-1, 1).
        static HK_FORCE_INLINE hkReal asin(hkReal r)
        {
            if (fabs(r) >= 1.0f) return (r > 0.0f) ? 0.5f * HK_REAL_PI : -0.5f * HK_REAL_PI;
            return ::asinf(r);
        }
        /// @brief acos, 0 at or above 1 and pi at or below -1.
        /// @note Out of line in vanilla (0x140b078e0; the IK solvers, hkbEvaluateExpressionModifier and BSLookAt call it).
        static hkReal acos(hkReal r);

        /// @brief Rounds toward negative infinity: truncate, then step down for a negative non-integer.
        /// @note Out-of-range input (truncation gives 0x80000000) and integers come back unchanged.
        static HK_FORCE_INLINE float floor(float r)
        {
            const __m128 v = _mm_set_ss(r);
            const int    i = _mm_cvtt_ss2si(v);
            if (i == int(0x80000000)) return r;
            if (_mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(i))) == r) return r;
            const int negative = _mm_movemask_ps(_mm_unpacklo_ps(v, v)) & 1;
            return _mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(i - negative)));
        }

        /// @brief 1/sqrt(r): rsqrtss plus one Newton-Raphson step, 0 for r == 0.
        static HK_FORCE_INLINE hkSimdReal sqrtInverse(hkSimdReal r)
        {
            const hkQuadReal x      = _mm_shuffle_ps(r.m_real, r.m_real, _MM_SHUFFLE(0, 0, 0, 0));
            const hkQuadReal e      = _mm_rsqrt_ss(x);
            const hkQuadReal isZero = _mm_cmpeq_ss(x, _mm_set_ss(0.0f));
            const hkQuadReal he     = _mm_mul_ss(_mm_set_ss(0.5f), e);
            const hkQuadReal ree    = _mm_mul_ss(_mm_mul_ss(x, e), e);
            const hkQuadReal inv    = _mm_andnot_ps(isZero, _mm_mul_ss(he, _mm_sub_ss(_mm_set_ss(3.0f), ree)));
            return hkSimdReal(_mm_shuffle_ps(inv, inv, _MM_SHUFFLE(0, 0, 0, 0)));
        }
    };

    HK_FORCE_INLINE void hkVector4::normalize3() { mul4(hkMath::sqrtInverse(lengthSquared3())); }

    /// @note hkMath::acos spelled inline: vanilla inlines it here (hkaRagdollInstance) though other units call the
    ///       out-of-line copy (0x140b078e0).
    HK_FORCE_INLINE hkReal hkQuaternion::getAngle() const
    {
        hkReal angle = hkMath::fabs(getReal());
        if (hkMath::fabs(angle) >= 1.0f) angle = (angle > 0.0f) ? 0.0f : HK_REAL_PI;
        else                             angle = ::acosf(angle);
        return angle * 2.0f;
    }
    /// @note The length is a scalar product (`mulss`): callers read it through getReal. The squared length is computed
    ///       twice (vanilla repeats the dot product for the inverse).
    HK_FORCE_INLINE hkSimdReal hkVector4::normalizeWithLength3()
    {
        const hkSimdReal len2   = lengthSquared3();
        const hkSimdReal invLen = hkMath::sqrtInverse(lengthSquared3());
        m_quad                  = _mm_mul_ps(m_quad, invLen.m_real);
        return hkSimdReal(_mm_mul_ss(len2.m_real, invLen.m_real));
    }

    HK_FORCE_INLINE void hkQsTransform::blendAddMul(const hkQsTransform& other, hkSimdReal weight)
    {
        m_translation.addMul4(weight, other.m_translation);
        m_scale.addMul4(other.m_scale, weight);
        const hkSimdReal signedWeight =
            (m_rotation.m_vec.dot4(other.m_rotation.m_vec).getReal() < 0.0f) ? -weight : weight;
        m_rotation.m_vec.addMul4(signedWeight, other.m_rotation.m_vec);
    }

    HK_FORCE_INLINE void hkQsTransform::blendNormalize(hkSimdReal totalWeight)
    {
        if (hkMath::fabs(totalWeight.getReal()) < 1.192092896e-07f) {
            setIdentity();
            return;
        }

        const hkQuadReal invWeight = _mm_div_ps(g_vectorConstantOne.m_quad, totalWeight.m_real);
        const hkSimdReal inv(_mm_shuffle_ps(invWeight, invWeight, _MM_SHUFFLE(0, 0, 0, 0)));
        m_translation.m_quad = _mm_mul_ps(inv.m_real, m_translation.m_quad);
        m_scale.m_quad       = _mm_mul_ps(inv.m_real, m_scale.m_quad);

        if (m_rotation.m_vec.lengthSquared4().getReal() < 1.192092896e-07f) m_rotation.setIdentity();
        else                                                                m_rotation.m_vec.normalize4();

        if (m_scale.lengthSquared3().getReal() < 1.192092896e-07f) m_scale.setAll(1.0f);
    }
} }
