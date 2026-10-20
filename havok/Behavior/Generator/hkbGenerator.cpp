/// @file
/// @brief hkbGenerator's default virtuals, reimplemented against vanilla (AE 1.6.1170).
/// @note Slots 21 and 27 are header-inline COMDAT copies in vanilla (0x140ab7db0, 0x140ab7e00), the rest sit after
///       hkbNode's out-of-line functions (0x140ac1c50–0x140ac1c80); defined here out of line either way.
#include <Behavior/Generator/hkbGenerator.h>

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>

namespace CB { namespace havok
{
    bool hkbGenerator::vfn21()
    {
        return true;
    }

    bool hkbGenerator::canRecycleOutput() const
    {
        return false;
    }

    void hkbGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        info.m_syncInfo.m_baseFrequency = 0.0f;
        info.m_syncInfo.m_localTime     = 0.0f;
        info.m_syncInfo.m_playbackSpeed = 1.0f;
        info.m_syncInfo.m_numSyncPoints = 0;
        info.m_syncInfo.m_isCyclic      = false;
        info.m_syncInfo.m_isMirrored    = false;
        info.m_syncInfo.m_isAdditive    = false;
    }

    void hkbGenerator::setElapsedTime(hkReal elapsed)
    {
    }

    void hkbGenerator::startEcho(hkReal echoDuration)
    {
    }
} }
