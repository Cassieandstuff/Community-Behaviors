/// @file
/// @brief BSPassByTargetTriggerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSPassByTargetTriggerModifier/BSPassByTargetTriggerModifier.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

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

    /// @note Every constructor sets bit 0 of m_userData; what it flags is unknown.
    BSPassByTargetTriggerModifier::BSPassByTargetTriggerModifier()
    :   m_targetPosition(0.0f, 0.0f, 0.0f, 0.0f),
        m_radius(0.0f),
        m_movementDirection(0.0f, 0.0f, 0.0f, 0.0f)
    {
        m_userData |= 1;
        m_targetPassed = true;
    }

    BSPassByTargetTriggerModifier::BSPassByTargetTriggerModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_triggerEvent(flag)
    {
        m_userData |= 1;
    }

    BSPassByTargetTriggerModifier::BSPassByTargetTriggerModifier(const BSPassByTargetTriggerModifier& mod)
    :   hkbModifier(mod),
        m_targetPosition(mod.m_targetPosition),
        m_radius(mod.m_radius),
        m_movementDirection(mod.m_movementDirection),
        m_triggerEvent(mod.m_triggerEvent),
        m_targetPassed(mod.m_targetPassed)
    {
        m_userData |= 1;
    }

    const void* BSPassByTargetTriggerModifier::getClassType() const
    {
        return &staticClass();
    }

    void BSPassByTargetTriggerModifier::activate(const hkbContext& context)
    {
        m_targetPassed = true;
    }

    bool BSPassByTargetTriggerModifier::isValid(hkStringPtr& errorString) const
    {
        return true;
    }

    hkbNode* BSPassByTargetTriggerModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSPassByTargetTriggerModifier(*this);
    }

    /// @note The singleton is re-read before each call, as vanilla does. "Passing" = the movement points away from the
    ///       target (negative dot product of the target direction and the movement); m_targetPassed holds last frame's
    ///       answer, so the event fires once per pass, and resets whenever the character is out of range.
    void BSPassByTargetTriggerModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("BSPassByTargetTriggerModifier");

        if (BSIPassByTargetTriggerModifierSingleton::ms_pSingleton && BSIPassByTargetTriggerModifierSingleton::ms_pSingleton->canTrigger(context)) {
            if (BSIPassByTargetTriggerModifierSingleton::ms_pSingleton) BSIPassByTargetTriggerModifierSingleton::ms_pSingleton->getTargetPosition(m_targetPosition);

            hkVector4 toTarget;
            toTarget.setSub4(m_targetPosition, context.m_character->m_worldFromModel->m_translation);
            const hkSimdReal distance = toTarget.normalizeWithLength3();

            if (distance.getReal() < m_radius) {
                if (BSIPassByTargetTriggerModifierSingleton::ms_pSingleton) BSIPassByTargetTriggerModifierSingleton::ms_pSingleton->getMovementDirection(context, m_movementDirection);
                m_movementDirection.normalize3();

                const bool passed = toTarget.dot3(m_movementDirection).getReal() < 0.0f;
                if (passed && !m_targetPassed && m_triggerEvent.m_id != -1) sendEvent(context, m_triggerEvent, this);
                m_targetPassed = passed;
            } else {
                m_targetPassed = false;
            }
        } else {
            m_targetPassed = false;
        }

        HK_TIMER_END();
    }
} }
