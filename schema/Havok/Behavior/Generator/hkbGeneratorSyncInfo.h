/// @file
/// @brief hkbGeneratorSyncInfo: what a generator reports about its timeline so a blender can synchronize its children.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbContext;

    /// @note 0x50 bytes (the blender copies it as a 0x50-byte block).
    struct hkbGeneratorSyncInfo
    {
        enum { MAX_SYNC_POINTS = 8 };

        struct SyncPoint
        {
            hkInt32 m_id;     ///< external event id
            hkReal  m_time;   ///< seconds of playback
        };

        SyncPoint m_syncPoints[MAX_SYNC_POINTS];   ///< +0x00
        hkReal    m_baseFrequency;                 ///< +0x40 — cycles per second; 0 when the generator doesn't advance
        hkReal    m_localTime;                     ///< +0x44
        hkReal    m_playbackSpeed;                 ///< +0x48
        hkInt8    m_numSyncPoints;                 ///< +0x4c
        hkBool    m_isCyclic;                      ///< +0x4d
        hkBool    m_isMirrored;                    ///< +0x4e
        hkBool    m_isAdditive;                    ///< +0x4f

        /// @brief Where a local time falls between two sync points: the ids of the sync points before and after it (-1
        ///        where there is none) and the fraction of the way from one to the other, in [0, 1].
        /// @note Name provisional; 12 bytes on the stack of hkbBlenderGenerator::updateSync.
        struct SyncPointInterval
        {
            hkInt32 m_beginSyncPointId;   ///< +0x0
            hkInt32 m_endSyncPointId;     ///< +0x4
            hkReal  m_fraction;           ///< +0x8
        };

        /// @brief Replaces every sync point's event id by its mirrored counterpart (the root graph's mirroring table)
        ///        and sets m_isMirrored.
        /// @note Name provisional. Lives outside the clip's TU (0x140ac2770).
        void mirror(const hkbContext& context);
        /// @brief The sync-point interval `localTime` falls in on this timeline.
        /// @note Name provisional; out of line (0x140ac2150).
        void computeSyncPointInterval(hkReal localTime, SyncPointInterval& intervalOut) const;
        /// @brief The local time on this timeline at the same place in `interval` (between the same sync point ids);
        ///        HK_FAILURE when this timeline lacks them.
        /// @note Name provisional; out of line (0x140ac2500).
        hkResult computeLocalTime(const SyncPointInterval& interval, hkReal& localTimeOut) const;
    };
    static_assert(sizeof(hkbGeneratorSyncInfo) == 0x50, "hkbGeneratorSyncInfo size");
} }
