/// @file
/// @brief BSGetTimeStepModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSGetTimeStepModifier/BSGetTimeStepModifier.h>

namespace CB { namespace havok
{
    BSGetTimeStepModifier::BSGetTimeStepModifier()
    :   m_timeStep(0.0f)
    {
        m_userData |= 2;
    }

    BSGetTimeStepModifier::BSGetTimeStepModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
        m_userData |= 2;
    }

    BSGetTimeStepModifier::BSGetTimeStepModifier(const BSGetTimeStepModifier& mod)
    :   hkbModifier(mod),
        m_timeStep(mod.m_timeStep)
    {
        m_userData |= 2;
    }

    void BSGetTimeStepModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timeStep = timestep;
    }

    hkbNode* BSGetTimeStepModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSGetTimeStepModifier(*this);
    }
} }
