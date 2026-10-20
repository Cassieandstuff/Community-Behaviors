/// @file
/// @brief hkbRagdollDriver, reimplemented against vanilla (AE 1.6.1170) — only what hkbCharacter calls so far.
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>

#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>

namespace CB { namespace havok
{
    hkbRagdollDriver::hkbRagdollDriver(hkbCharacter* character, bool removeRagdollFromWorld)
    :   m_defaultMode(0),
        m_removeRagdollFromWorld(removeRagdollFromWorld),
        m_flag1D(false),
        m_lastWorldFromModel(hkQsTransform::IDENTITY),
        m_int68(0),
        m_character(character),
        m_ragdoll(HK_NULL),
        m_ragdollPoseWS(HK_NULL),
        m_ragdollController(HK_NULL),
        m_scratch(HK_NULL),
        m_lastPoseLocal(HK_NULL),
        m_numLastPoseLocal(0),
        m_timeRigidBodyControllerActive(0.0f),
        m_timePoweredControllerActive(0.0f),
        m_counter(0),
        m_ragdollChanged(true),
        m_flagC5(false),
        m_enabled(true),
        m_flagCB(false)
    {
        m_flagsC7[0] = false;
        m_flagsC7[1] = false;
        m_flagsC7[2] = false;
        m_flagsC7[3] = false;
        m_mode       = m_defaultMode;
        m_worldFromModelModeData.m_poseMatchingBone0 = -1;
        m_worldFromModelModeData.m_poseMatchingBone1 = -1;
        m_worldFromModelModeData.m_poseMatchingBone2 = -1;
        m_worldFromModelModeData.m_mode              = 0;
        m_worldFromModelModeDataInUse                = m_worldFromModelModeData;
    }

    hkbRagdollDriver::~hkbRagdollDriver()
    {
        setRagdoll(HK_NULL);
        if (m_lastPoseLocal) hkDeallocateChunk(m_lastPoseLocal, m_numLastPoseLocal);
        if (m_scratch) hkDeallocate(m_scratch);
    }

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

    void hkbRagdollDriver::extractRagdollPose(const hkbContext& context, hkbGeneratorOutput& inOut, bool computeWorldFromModel,
                                              int poseMatchingBone0, int poseMatchingBone1, int poseMatchingBone2)
    {
        extractRagdollPoseInternal(context, inOut, 0.0f, 0.0f, false, computeWorldFromModel, poseMatchingBone0, poseMatchingBone1,
                                   poseMatchingBone2);
    }
} }
