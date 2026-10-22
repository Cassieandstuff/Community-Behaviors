/// @file
/// @brief hkaFootstepAnalysisInfo and its container, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Animation/hkaFootstepAnalysisInfo.h>

namespace CB { namespace havok
{
    hkaFootstepAnalysisInfo::hkaFootstepAnalysisInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_name(flag),
        m_nameStrike(flag),
        m_nameLift(flag),
        m_nameLock(flag),
        m_nameUnlock(flag),
        m_minPos(flag),
        m_maxPos(flag),
        m_minVel(flag),
        m_maxVel(flag),
        m_allBonesDown(flag),
        m_anyBonesDown(flag)
    {
    }

    hkaFootstepAnalysisInfo::~hkaFootstepAnalysisInfo()
    {
    }

    hkaFootstepAnalysisInfoContainer::~hkaFootstepAnalysisInfoContainer()
    {
    }
} }
