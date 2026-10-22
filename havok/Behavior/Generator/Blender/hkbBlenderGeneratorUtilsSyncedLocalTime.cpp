/// @file
/// @brief hkbBlenderGeneratorUtils::computeSyncedLocalTime, reimplemented against vanilla (AE 1.6.1170).
/// @note A matching device, named so it can't pass for a Havok file: vanilla compiles the function (0x140b0ca10) in
///       hkbBlenderGeneratorUtils' unit, right after computeSyncInfo, but the blender's work owns that unit and its only
///       caller is hkbBlendingTransitionEffect. Merge it into hkbBlenderGeneratorUtils.cpp once that file exists.
#include <Behavior/Generator/Blender/hkbBlenderGeneratorUtils.h>

#include <Behavior/Generator/hkbGeneratorSyncInfo.h>

namespace CB { namespace havok
{
    namespace hkbBlenderGeneratorUtils
    {
        hkReal computeSyncedLocalTime(hkReal localTime, const hkbGeneratorSyncInfo& syncInfo, const hkbGeneratorSyncInfo& childSyncInfo)
        {
            hkLocalBuffer<const hkbGeneratorSyncInfo*> syncInfos(2);
            syncInfos[0] = &syncInfo;
            syncInfos[1] = &childSyncInfo;

            int       syncPointIds[hkbGeneratorSyncInfo::MAX_SYNC_POINTS];
            const int numSyncPointIds = findCommonSyncPointIds(&syncInfos[0], 2, syncPointIds);

            hkbGeneratorSyncInfo::SyncPointInterval interval;
            syncInfo.computeSyncPointInterval(localTime, syncPointIds, numSyncPointIds, interval);

            hkReal childLocalTime = 0.0f;
            childSyncInfo.computeLocalTime(interval, childLocalTime);
            return childLocalTime;
        }
    }
} }
