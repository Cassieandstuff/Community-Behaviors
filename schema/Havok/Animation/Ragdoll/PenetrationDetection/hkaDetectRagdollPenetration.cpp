/// @file
/// @brief hkaDetectRagdollPenetration, reimplemented against vanilla (AE 1.6.1170): the functions matched so far.
#include <Animation/Ragdoll/PenetrationDetection/hkaDetectRagdollPenetration.h>

namespace CB { namespace schema
{
    hkaDetectRagdollPenetration::hkaDetectRagdollPenetration(const Setup& setup)
    :   m_setup(setup)
    {
        m_numBones = m_setup.m_ragdollSkeleton->m_bones.getSize();
        m_boneStates.setSize(m_numBones, HK_YESP);
        m_ragdollIsOut = false;
    }
} }
