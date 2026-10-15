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

        /// @brief Replaces every sync point's event id by its mirrored counterpart (the root graph's mirroring table)
        ///        and sets m_isMirrored.
        /// @note Name provisional. Lives outside the clip's TU (0x140ac2770).
        void mirror(const hkbContext& context);
    };
    static_assert(sizeof(hkbGeneratorSyncInfo) == 0x50, "hkbGeneratorSyncInfo size");
} }
