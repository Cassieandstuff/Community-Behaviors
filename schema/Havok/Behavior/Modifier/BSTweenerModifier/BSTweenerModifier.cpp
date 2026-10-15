/// @file
/// @brief BSTweenerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSTweenerModifier/BSTweenerModifier.h>

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    BSTweenerModifier::BSTweenerModifier()
    :   m_tweenPosition(true),
        m_tweenRotation(true),
        m_useTweenDuration(false),
        m_tweenDuration(0.0f),
        m_targetPosition(0.0f, 0.0f, 0.0f, 0.0f)
    {
        m_targetRotation.m_vec.set(0.0f, 0.0f, 0.0f, 1.0f);
        m_duration = 0.0f;
        m_startTransform.m_translation.setZero4();
        m_startTransform.m_rotation.m_vec.setZero4();
        m_startTransform.m_scale.setZero4();
        m_time = 0.0f;
    }

    BSTweenerModifier::BSTweenerModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
    }

    BSTweenerModifier::BSTweenerModifier(const BSTweenerModifier& mod)
    :   hkbModifier(mod),
        m_tweenPosition(mod.m_tweenPosition),
        m_tweenRotation(mod.m_tweenRotation),
        m_useTweenDuration(mod.m_useTweenDuration),
        m_tweenDuration(mod.m_tweenDuration),
        m_targetPosition(mod.m_targetPosition),
        m_targetRotation(mod.m_targetRotation),
        m_duration(mod.m_duration),
        m_startTransform(mod.m_startTransform),
        m_time(mod.m_time)
    {
    }

    const void* BSTweenerModifier::getClassType() const
    {
        return &staticClass();
    }

    void BSTweenerModifier::activate(const hkbContext& context)
    {
        m_startTransform = *context.m_character->m_worldFromModel;
        m_time           = 0.0f;
        m_duration       = 0.0f;
    }

    void BSTweenerModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_time += timestep;
    }

    bool BSTweenerModifier::isValid(hkStringPtr& errorString) const
    {
        return true;
    }

    hkbNode* BSTweenerModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSTweenerModifier(*this);
    }

    /// @note t = clamp(m_time / m_duration, 0, 1), 0 while the duration is unset. The position's t enters as a
    ///       single-lane hkSimdReal (`_mm_set_ss`): with the broadcast float (hkSimdReal(t)) VS2015 schedules the target
    ///       load before setInterpolate4's 1-vector, the reverse of vanilla (same arithmetic).
    void BSTweenerModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("BSTweenerModifier");

        hkReal t = 0.0f;
        if (m_duration != 0.0f) {
            const hkReal fraction = m_time / m_duration;
            if (fraction >= 0.0f) t = hkMath::min2(fraction, 1.0f);
        }

        if (m_tweenPosition) inOut.accessWorldFromModel().m_translation.setInterpolate4(m_startTransform.m_translation, m_targetPosition, hkSimdReal(_mm_set_ss(t)));
        if (m_tweenRotation) {
            hkQuaternion rotation;
            interpolateRotation(m_startTransform.m_rotation, m_targetRotation, t, rotation);
            inOut.accessWorldFromModel().m_rotation = rotation;
        }

        HK_TIMER_END();
    }

    /// @note Without m_useTweenDuration the tween lasts one cycle of the wrapped generator; a generator that doesn't
    ///       advance (frequency 0) leaves the duration as it was.
    void BSTweenerModifier::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        if (m_useTweenDuration) {
            m_duration = m_tweenDuration;
        } else {
            const hkReal frequency = info.m_syncInfo.m_baseFrequency;
            if (frequency != 0.0f) m_duration = 1.0f / frequency;
        }
    }

    /// @note The result is rebuilt from its four floats (vanilla's unpcklps tree).
    void BSTweenerModifier::interpolateRotation(const hkQuaternion& from, const hkQuaternion& to, hkReal t, hkQuaternion& out)
    {
        hkReal sign = 1.0f;
        if (to.m_vec.dot4(from.m_vec).getReal() < 0.0f) sign = -1.0f;

        hkVector4 blend;
        blend.setMul4(to.m_vec, hkSimdReal(sign * t));
        blend.addMul4(from.m_vec, hkSimdReal(1.0f - t));
        blend.normalize4();

        const hkQuadReal q = blend.m_quad;
        out.m_vec.set(_mm_cvtss_f32(q), _mm_cvtss_f32(_mm_shuffle_ps(q, q, _MM_SHUFFLE(1, 1, 1, 1))),
                      _mm_cvtss_f32(_mm_shuffle_ps(q, q, _MM_SHUFFLE(2, 2, 2, 2))), _mm_cvtss_f32(_mm_shuffle_ps(q, q, _MM_SHUFFLE(3, 3, 3, 3))));
    }
} }
