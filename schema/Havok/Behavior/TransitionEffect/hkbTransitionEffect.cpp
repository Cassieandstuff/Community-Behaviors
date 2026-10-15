/// @file
/// @brief hkbTransitionEffect, reimplemented against vanilla (AE 1.6.1170).
/// @note The file name is inferred from the exe's TransitionEffect\Generator\hkbGeneratorTransitionEffect.cpp. Vanilla's
///       unit runs 0x140ae5d80-0x140ae5fbf.
#include <Behavior/TransitionEffect/hkbTransitionEffect.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/BehaviorReference/hkbBehaviorReferenceGenerator.h>
#include <Behavior/Generator/ModifierGenerator/hkbModifierGenerator.h>
#include <Common/Serialize/Util/hkBuiltinTypeRegistry.h>

namespace CB { namespace schema
{
    hkbTransitionEffect::hkbTransitionEffect()
    :   m_selfTransitionMode(SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC),
        m_eventMode(EVENT_MODE_DEFAULT),
        m_defaultEventMode(EVENT_MODE_IGNORE_FROM_GENERATOR)
    {
    }

    hkbTransitionEffect::hkbTransitionEffect(const hkbTransitionEffect& te)
    :   hkbGenerator(te),
        m_selfTransitionMode(te.m_selfTransitionMode),
        m_eventMode(te.m_eventMode),
        m_defaultEventMode(EVENT_MODE_IGNORE_FROM_GENERATOR)
    {
    }

    bool hkbTransitionEffect::isTransitionEffect() const
    {
        return true;
    }

    void hkbTransitionEffect::applySelfTransitionMode(hkbBehaviorGraph& rootBehavior, hkbGenerator* toGenerator)
    {
        SelfTransitionMode mode;
        if (m_selfTransitionMode == SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC) {
            const hkbNodeInfo& info = rootBehavior.getNodeInfo(toGenerator);
            mode = info.m_syncInfo.m_isCyclic ? SELF_TRANSITION_MODE_CONTINUE : SELF_TRANSITION_MODE_BLEND;
        } else {
            mode = static_cast<SelfTransitionMode>(m_selfTransitionMode);
        }

        switch (mode) {
            case SELF_TRANSITION_MODE_RESET: rootBehavior.startEcho(0.0f, toGenerator); break;
            case SELF_TRANSITION_MODE_BLEND: rootBehavior.startEcho(getToGeneratorBlendInTime(), toGenerator); break;
        }
    }

    hkbTransitionEffect::EventMode hkbTransitionEffect::getEventMode() const
    {
        return static_cast<EventMode>((m_eventMode == EVENT_MODE_DEFAULT) ? m_defaultEventMode : m_eventMode);
    }

    bool hkbTransitionEffect::isGeneratorActive(const hkbContext& context, hkbGenerator* generator)
    {
        for (;;) {
            const hkClass* cls = hkBuiltinTypeRegistry::getInstance().getVtableClassRegistry()->getClassFromVirtualInstance(generator);
            if (cls == &hkbModifierGeneratorClass) {
                generator = static_cast<hkbModifierGenerator*>(generator)->m_generator;
                continue;
            }
            if (cls == &hkbBehaviorReferenceGeneratorClass) {
                generator = static_cast<hkbBehaviorReferenceGenerator*>(generator)->m_behavior;
                continue;
            }
            break;
        }
        return context.isNodeActive(generator);
    }
} }
