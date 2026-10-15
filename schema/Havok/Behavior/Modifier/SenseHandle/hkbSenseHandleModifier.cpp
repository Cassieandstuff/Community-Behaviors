/// @file
/// @brief hkbSenseHandleModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/SenseHandle/hkbSenseHandleModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace schema
{
    /// @note The 0x17-byte memCpy copies m_minDistance through m_keepFirstSensedHandle.
    hkbSenseHandleModifier::hkbSenseHandleModifier(const hkbSenseHandleModifier& mod)
    :   hkbModifier(mod),
        m_localFrameName(mod.m_localFrameName),
        m_sensorLocalFrameName(mod.m_sensorLocalFrameName)
    {
        m_sensorLocalOffset = mod.m_sensorLocalOffset;
        m_ranges            = mod.m_ranges;
        hkString::memCpy(&m_minDistance, &mod.m_minDistance, 0x17);
        m_distanceOut    = 0.0f;
        m_foundHandleOut = false;
        m_handle.clear();
    }

    void hkbSenseHandleModifier::activate(const hkbContext& context)
    {
        m_distanceOut                        = 0.0f;
        m_foundHandleOut                     = false;
        m_timeSinceLastModify                = 0.0f;
        m_rangeIndexForEventToSendNextUpdate = -1;
        m_handle.clear();
    }

    /// @note The range's event is raised one update after modify picked it.
    void hkbSenseHandleModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timeSinceLastModify += timestep;

        if (m_rangeIndexForEventToSendNextUpdate != -1) {
            hkbCharacter*     character = context.m_character;
            hkbBehaviorGraph* behavior  = character->m_behaviorGraph;
            const Range&      range     = m_ranges[m_rangeIndexForEventToSendNextUpdate];
            hkbEventQueue*    queue     = context.m_eventQueue ? context.m_eventQueue : character->m_eventQueue;
            hkbNode*          sender    = behavior->getNodeTemplate(this);
            hkbEvent          event(range.m_event.m_id, range.m_event.m_payload, sender);
            event.m_id = queue->toExternal(event.m_id);
            queue->enqueue(event);
            m_rangeIndexForEventToSendNextUpdate = -1;
        }
    }

    void hkbSenseHandleModifier::deactivate(const hkbContext& context)
    {
        m_handle.clear();
        m_handleIn  = HK_NULL;
        m_handleOut = HK_NULL;
    }

    bool hkbSenseHandleModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;

        const bool usingRagdollBone   = m_sensorRagdollBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("sensorRagdollBoneIndex"));
        const bool usingAnimationBone = m_sensorAnimationBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("sensorAnimationBoneIndex"));

        if (usingRagdollBone) {
            if (usingAnimationBone) {
                errorString = "You cannot specify both a ragdoll bone and an animation bone for sensing.";
                return false;
            }
        } else if (m_extrapolateSensorPosition) {
            errorString = "You can only extrapolate the sensor position when using a ragdoll bone for sensing.";
            return false;
        }

        for (int i = 0; i < m_ranges.getSize(); i++) {
            if (m_ranges[i].m_minDistance > m_ranges[i].m_maxDistance) {
                errorString = "The minimum distance between the sensor position and the sensed handle cannot be greater than the maximum distance.";
                return false;
            }
            if (m_ranges[i].m_maxDistance <= 0.0f) {
                errorString = "The maximum distance to a handle must be greater than zero.";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbSenseHandleModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbSenseHandleModifier(*this);
    }

    void hkbSenseHandleModifier::computeMinMaxDistances(hkReal& minDistance, hkReal& maxDistance) const
    {
        maxDistance = m_maxDistance;
        minDistance = m_minDistance;
        const int numRanges = m_ranges.getSize();
        for (int i = 0; i < numRanges; i++) {
            if (m_ranges[i].m_maxDistance > maxDistance) maxDistance = m_ranges[i].m_maxDistance;
            if (m_ranges[i].m_minDistance < minDistance) minDistance = m_ranges[i].m_minDistance;
        }
    }

    void hkbSenseHandleModifier::processSensedHandle()
    {
        if (m_handle.m_rigidBody || m_handle.m_character) {
            if (m_handleOut) m_handleOut->set(m_handle);
            m_foundHandleOut = true;

            const int numRanges = m_ranges.getSize();
            for (int i = 0; i < numRanges; i++) {
                if (m_distanceOut > m_ranges[i].m_minDistance && m_distanceOut <= m_ranges[i].m_maxDistance) {
                    m_rangeIndexForEventToSendNextUpdate = i;
                    if (m_ranges[i].m_ignoreHandle) {
                        if (m_handleOut) m_handleOut->clear();
                        m_foundHandleOut = false;
                    }
                    break;
                }
            }
        } else {
            if (m_handleOut) m_handleOut->clear();
            m_foundHandleOut = false;
        }
    }
} }
