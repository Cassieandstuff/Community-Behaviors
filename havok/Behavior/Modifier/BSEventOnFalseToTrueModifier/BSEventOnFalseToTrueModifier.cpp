/// @file
/// @brief BSEventOnFalseToTrueModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSEventOnFalseToTrueModifier/BSEventOnFalseToTrueModifier.h>

namespace CB { namespace havok
{
    namespace
    {
        /// @brief Raises `e` from `sender` into the context's queue, its id translated to the queue's.
        HK_FORCE_INLINE void sendEvent(const hkbContext& context, const hkbEventProperty& e, hkbNode* sender)
        {
            hkbEventQueue* queue = context.getEventQueue();
            hkbEvent       event(e.m_id, e.m_payload, sender);
            event.m_id = queue->toExternal(event.m_id);
            queue->enqueue(event);
        }
    }

    /// @note Every constructor sets bit 1 of m_userData; what it flags is unknown. VARIANT only because vanilla schedules
    ///       hkbNode's m_cloneState store after the vtable load (as in the other node default constructors).
    BSEventOnFalseToTrueModifier::BSEventOnFalseToTrueModifier()
    :   m_bEnableEvent1(false),
        m_bVariableToTest1(false),
        m_bEnableEvent2(false),
        m_bVariableToTest2(false),
        m_bEnableEvent3(false),
        m_bVariableToTest3(false)
    {
        m_userData |= 2;
        m_bSlot1ActivatedLastFrame = false;
        m_bSlot2ActivatedLastFrame = false;
        m_bSlot3ActivatedLastFrame = false;
    }

    BSEventOnFalseToTrueModifier::BSEventOnFalseToTrueModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_EventToSend1(flag),
        m_EventToSend2(flag),
        m_EventToSend3(flag)
    {
        m_bSlot1ActivatedLastFrame = false;
        m_bSlot2ActivatedLastFrame = false;
        m_userData |= 2;
        m_bSlot3ActivatedLastFrame = false;
    }

    BSEventOnFalseToTrueModifier::BSEventOnFalseToTrueModifier(const BSEventOnFalseToTrueModifier& mod)
    :   hkbModifier(mod),
        m_bEnableEvent1(mod.m_bEnableEvent1),
        m_bVariableToTest1(mod.m_bVariableToTest1),
        m_EventToSend1(mod.m_EventToSend1),
        m_bEnableEvent2(mod.m_bEnableEvent2),
        m_bVariableToTest2(mod.m_bVariableToTest2),
        m_EventToSend2(mod.m_EventToSend2),
        m_bEnableEvent3(mod.m_bEnableEvent3),
        m_bVariableToTest3(mod.m_bVariableToTest3),
        m_EventToSend3(mod.m_EventToSend3)
    {
        m_userData |= 2;
        m_bSlot1ActivatedLastFrame = false;
        m_bSlot2ActivatedLastFrame = false;
        m_bSlot3ActivatedLastFrame = false;
    }

    const void* BSEventOnFalseToTrueModifier::getClassType() const
    {
        return &staticClass();
    }

    /// @note Without a queue nothing is raised, but the last-frame flags still update.
    void BSEventOnFalseToTrueModifier::update(const hkbContext& context, hkReal timestep)
    {
        if (context.getEventQueue()) {
            if (!m_bSlot1ActivatedLastFrame && m_bEnableEvent1 && m_bVariableToTest1 && m_EventToSend1.m_id != -1) sendEvent(context, m_EventToSend1, this);
            if (!m_bSlot2ActivatedLastFrame && m_bEnableEvent2 && m_bVariableToTest2 && m_EventToSend2.m_id != -1) sendEvent(context, m_EventToSend2, this);
            if (!m_bSlot3ActivatedLastFrame && m_bEnableEvent3 && m_bVariableToTest3 && m_EventToSend3.m_id != -1) sendEvent(context, m_EventToSend3, this);
        }

        m_bSlot1ActivatedLastFrame = m_bEnableEvent1 && m_bVariableToTest1;
        m_bSlot2ActivatedLastFrame = m_bEnableEvent2 && m_bVariableToTest2;
        m_bSlot3ActivatedLastFrame = m_bEnableEvent3 && m_bVariableToTest3;
    }

    bool BSEventOnFalseToTrueModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_EventToSend1.m_id == -1 && m_EventToSend2.m_id == -1 && m_EventToSend3.m_id == -1) errorString = "At least one event has to be valid.";
        return errorString.getLength() == 0;
    }

    hkbNode* BSEventOnFalseToTrueModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSEventOnFalseToTrueModifier(*this);
    }
} }
