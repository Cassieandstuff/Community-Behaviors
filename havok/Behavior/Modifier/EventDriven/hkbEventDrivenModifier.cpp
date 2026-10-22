/// @file
/// @brief hkbEventDrivenModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/EventDriven/hkbEventDrivenModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>

namespace CB { namespace havok
{
    hkbEventDrivenModifier::hkbEventDrivenModifier()
    :   m_activateEventId(-1),
        m_deactivateEventId(-1),
        m_activeByDefault(false),
        m_isActive(false)
    {
        m_userData |= 1;
    }

    hkbEventDrivenModifier::hkbEventDrivenModifier(const hkbEventDrivenModifier& mod)
    :   hkbModifierWrapper(mod),
        m_activateEventId(mod.m_activateEventId),
        m_deactivateEventId(mod.m_deactivateEventId),
        m_activeByDefault(mod.m_activeByDefault),
        m_isActive(false)
    {
        m_userData |= 1;
    }

    const void* hkbEventDrivenModifier::getClassType() const
    {
        return &staticClass();
    }

    void hkbEventDrivenModifier::activate(const hkbContext& context)
    {
        m_isActive = m_activeByDefault;
    }

    void hkbEventDrivenModifier::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (!(flags & FLAG_GENERATORS_ONLY) && (!(flags & FLAG_ACTIVE_ONLY) || (m_isActive && m_enable))) {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node = m_modifier;
        }
    }

    /// @note Vanilla stores `(m_updateActiveNodes != 0) | 1` (hkbDelayedModifier::update's `changed | flag` with
    ///       `changed` known true); VS2015 24247 folds that to the constant, so the plain store is written here.
    void hkbEventDrivenModifier::handleEvent(const hkbContext& context, const hkbEvent e)
    {
        if (m_isActive) {
            if (!m_modifier || m_deactivateEventId == -1 || m_deactivateEventId != e.getId()) return;
            m_isActive = false;
        } else {
            if (!m_modifier || m_activateEventId == -1 || m_activateEventId != e.getId()) return;
            m_isActive = true;
        }

        hkbBehaviorGraph* rootBehavior    = context.m_character->m_behaviorGraph;
        rootBehavior->m_updateActiveNodes = true;
        context.m_eventTriggeredTransition |= hkbContext::FLAG_ACTIVE_NODES_CHANGED;
    }

    hkbNode* hkbEventDrivenModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbEventDrivenModifier(*this);
    }

    void hkbEventDrivenModifier::getActiveEvents(hkPointerMap<int, bool>& activeEvents) const
    {
        if (m_activateEventId >= 0) activeEvents.insert(m_activateEventId, true);
        if (m_deactivateEventId >= 0) activeEvents.insert(m_deactivateEventId, true);
    }

    void hkbEventDrivenModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if (m_isActive) callModify(context, m_modifier, inOut);
    }

    void hkbEventDrivenModifier::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        if (m_isActive && m_modifier->m_enable) {
            hkbModifier* clone = static_cast<hkbModifier*>(context.m_character->m_behaviorGraph->getNodeClone(m_modifier));
            clone->updateSync(context, info);
        }
    }

    hkReferencedObject* hkbEventDrivenModifier::createInternalState()
    {
        return new hkbEventDrivenModifierInternalState;
    }

    void hkbEventDrivenModifier::getInternalState(hkReferencedObject& internalState) const
    {
        static_cast<hkbEventDrivenModifierInternalState&>(internalState).m_isActive = m_isActive;
    }

    void hkbEventDrivenModifier::setInternalState(const hkReferencedObject& internalState)
    {
        m_isActive = static_cast<const hkbEventDrivenModifierInternalState&>(internalState).m_isActive;
    }
} }
