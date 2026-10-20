/// @file
/// @brief BSSpeedSamplerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSSpeedSamplerModifier/BSSpeedSamplerModifier.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace havok
{
    BSSpeedSamplerModifier::BSSpeedSamplerModifier()
    :   m_state(-1),
        m_direction(0.0f),
        m_goalSpeed(0.0f),
        m_speedOut(0.0f)
    {
        m_userData |= 2;
    }

    BSSpeedSamplerModifier::BSSpeedSamplerModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
        m_userData |= 2;
    }

    BSSpeedSamplerModifier::BSSpeedSamplerModifier(const BSSpeedSamplerModifier& mod)
    :   hkbModifier(mod),
        m_state(mod.m_state),
        m_direction(mod.m_direction),
        m_goalSpeed(mod.m_goalSpeed),
        m_speedOut(mod.m_speedOut)
    {
        m_userData |= 2;
    }

    void BSSpeedSamplerModifier::update(const hkbContext& context, hkReal timestep)
    {
        HK_TIMER_BEGIN("BSSpeedSamplerModifier");
        hkReal speedOut = m_goalSpeed;
        if (BSISpeedSamplerDB::s_instance) speedOut = BSISpeedSamplerDB::s_instance->getSpeedOut(context, m_state, m_direction, m_goalSpeed);
        m_speedOut = speedOut;
        HK_TIMER_END();
    }

    hkbNode* BSSpeedSamplerModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSSpeedSamplerModifier(*this);
    }
} }
