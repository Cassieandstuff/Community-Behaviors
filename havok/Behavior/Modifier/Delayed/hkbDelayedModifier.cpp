/// @file
/// @brief hkbDelayedModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Delayed/hkbDelayedModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>

namespace CB { namespace havok
{
    hkbDelayedModifier::hkbDelayedModifier()
    :   m_delaySeconds(0.0f),
        m_durationSeconds(0.0f),
        m_secondsElapsed(0.0f),
        m_isActive(false)
    {
    }

    hkbDelayedModifier::hkbDelayedModifier(const hkbDelayedModifier& mod)
    :   hkbModifierWrapper(mod),
        m_delaySeconds(mod.m_delaySeconds),
        m_durationSeconds(mod.m_durationSeconds),
        m_secondsElapsed(0.0f),
        m_isActive(false)
    {
    }

    void hkbDelayedModifier::activate(const hkbContext& context)
    {
        m_secondsElapsed = 0.0f;
        m_isActive       = isDelayElapsed();
    }

    void hkbDelayedModifier::update(const hkbContext& context, hkReal timestep)
    {
        const bool wasActive = m_isActive;
        m_secondsElapsed += timestep;
        m_isActive = isDelayElapsed();

        hkbBehaviorGraph* rootBehavior   = context.m_character->m_behaviorGraph;
        rootBehavior->m_updateActiveNodes = (wasActive != m_isActive) | rootBehavior->m_updateActiveNodes;
    }

    void hkbDelayedModifier::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (!(flags & FLAG_GENERATORS_ONLY) && (!(flags & FLAG_ACTIVE_ONLY) || (m_isActive && m_enable))) {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node = m_modifier;
        }
    }

    hkbNode* hkbDelayedModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbDelayedModifier(*this);
    }

    void hkbDelayedModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if (m_isActive) callModify(context, m_modifier, inOut);
    }

    void hkbDelayedModifier::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        if (m_isActive && m_modifier->m_enable) {
            hkbModifier* clone = static_cast<hkbModifier*>(context.m_character->m_behaviorGraph->getNodeClone(m_modifier));
            clone->updateSync(context, info);
        }
    }

    bool hkbDelayedModifier::isDelayElapsed() const
    {
        if (m_modifier && m_secondsElapsed >= m_delaySeconds && (m_durationSeconds == 0.0f || m_secondsElapsed - m_delaySeconds <= m_durationSeconds)) {
            return true;
        }
        return false;
    }

    hkReferencedObject* hkbDelayedModifier::createInternalState()
    {
        return new hkbDelayedModifierInternalState;
    }

    void hkbDelayedModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbDelayedModifierInternalState& state = static_cast<hkbDelayedModifierInternalState&>(internalState);
        state.m_secondsElapsed                 = m_secondsElapsed;
        state.m_isActive                       = m_isActive;
    }

    void hkbDelayedModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbDelayedModifierInternalState& state = static_cast<const hkbDelayedModifierInternalState&>(internalState);
        m_secondsElapsed                             = state.m_secondsElapsed;
        m_isActive                                   = state.m_isActive;
    }
} }
