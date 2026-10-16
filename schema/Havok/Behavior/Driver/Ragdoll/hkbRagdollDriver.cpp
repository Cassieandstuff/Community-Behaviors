/// @file
/// @brief hkbRagdollDriver, reimplemented against vanilla (AE 1.6.1170) — only what hkbCharacter calls so far.
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>

#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>

namespace CB { namespace schema
{
    void hkbRagdollDriver::reset()
    {
        setRagdoll(m_ragdoll);
        if (m_lastPoseLocal) {
            hkDeallocateChunk(m_lastPoseLocal, m_numLastPoseLocal);
            m_lastPoseLocal    = HK_NULL;
            m_numLastPoseLocal = 0;
        }
        freeRagdollPoseWS();
        if (m_scratch) {
            hkDeallocate(m_scratch);
            m_scratch = HK_NULL;
        }
        m_counter                       = 0;
        m_mode                          = m_defaultMode;
        m_lastWorldFromModel            = hkQsTransform::getIdentity();
        m_timeRigidBodyControllerActive = 0.0f;
        m_timePoweredControllerActive   = 0.0f;
        m_ragdollChanged                = true;
        m_flagC5                        = false;
        m_flagsC7[0]                    = false;
        m_flagsC7[1]                    = false;
        m_flagsC7[2]                    = false;
        m_flagsC7[3]                    = false;
    }

    void hkbRagdollDriver::addAttachedRigidBody(hkpRigidBody* rigidBody)
    {
        if (m_ragdoll) {
            if (m_ragdoll->getBoneIndexOfRigidBody(rigidBody) != -1) m_attachedRigidBodyToIndexMap.insert(rigidBody, 1);
        }
    }

    void hkbRagdollDriver::removeAttachedRigidBody(hkpRigidBody* rigidBody)
    {
        if (m_ragdoll) m_attachedRigidBodyToIndexMap.remove(rigidBody);
    }
} }
