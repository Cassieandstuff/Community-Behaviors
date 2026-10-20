/// @file
/// @brief BSEventOnDeactivateModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSEventOnDeactivateModifier/BSEventOnDeactivateModifier.h>

namespace CB { namespace havok
{
    BSEventOnDeactivateModifier::BSEventOnDeactivateModifier()
    {
        m_userData |= 3;
    }

    BSEventOnDeactivateModifier::BSEventOnDeactivateModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_event(flag)
    {
        m_userData |= 3;
    }

    BSEventOnDeactivateModifier::BSEventOnDeactivateModifier(const BSEventOnDeactivateModifier& mod)
    :   hkbModifier(mod),
        m_event(mod.m_event)
    {
        m_userData |= 3;
    }

    /// @note Sent by the node itself, with no check of the id (isValid rejects an unset one); nothing is sent when
    ///       neither the context nor the character has a queue.
    void BSEventOnDeactivateModifier::deactivate(const hkbContext& context)
    {
        hkbEventQueue* queue = context.getEventQueue();
        if (queue) {
            hkbEvent event(m_event.m_id, m_event.m_payload, this);
            event.m_id = queue->toExternal(event.m_id);
            queue->enqueue(event);
        }
    }

    bool BSEventOnDeactivateModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_event.m_id == -1) errorString = "Event On Deactivate Modifier needs event set.";
        return m_event.m_id != -1;
    }

    hkbNode* BSEventOnDeactivateModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSEventOnDeactivateModifier(*this);
    }
} }
