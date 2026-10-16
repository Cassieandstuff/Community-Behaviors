/// @file
/// @brief BSInterpValueModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSInterpValueModifier/BSInterpValueModifier.h>
#include <Behavior/Modifier/BSLookAt/BSLookAtModifierUtils.h>

namespace CB { namespace schema
{
    BSInterpValueModifier::BSInterpValueModifier()
    :   m_source(0.0f),
        m_target(0.0f),
        m_result(0.0f),
        m_gain(0.0f),
        m_timeStep(0.0f)
    {
        m_userData |= 2;
    }

    BSInterpValueModifier::BSInterpValueModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
        m_userData |= 2;
    }

    BSInterpValueModifier::BSInterpValueModifier(const BSInterpValueModifier& mod)
    :   hkbModifier(mod),
        m_source(mod.m_source),
        m_target(mod.m_target),
        m_result(mod.m_result),
        m_gain(mod.m_gain),
        m_timeStep(mod.m_timeStep)
    {
        m_userData |= 2;
    }

    const void* BSInterpValueModifier::getClassType() const
    {
        return &staticClass();
    }

    void BSInterpValueModifier::activate(const hkbContext& context)
    {
        m_timeStep = 0.0f;
    }

    /// @note The source is read before the gain call, the target after it.
    void BSInterpValueModifier::update(const hkbContext& context, hkReal timestep)
    {
        const hkReal source = m_source;
        m_timeStep          = timestep;
        m_result            = source + BSComputeGainForTimeStep(m_gain, timestep) * (m_target - source);
    }

    bool BSInterpValueModifier::isValid(hkStringPtr& errorString) const
    {
        return true;
    }

    hkbNode* BSInterpValueModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSInterpValueModifier(*this);
    }
} }
