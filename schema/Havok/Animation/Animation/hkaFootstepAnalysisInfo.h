/// @file
/// @brief hkaFootstepAnalysisInfo (one animation's footstep analysis settings) and its container.
/// @note Header path and every member name provisional (the layout is vanilla's: five char arrays, six real arrays,
///       three reals).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaFootstepAnalysisInfo : hkReferencedObject
    {
        hkArray<char>   m_name;           ///< +0x10
        hkArray<char>   m_nameStrike;     ///< +0x20
        hkArray<char>   m_nameLift;       ///< +0x30
        hkArray<char>   m_nameLock;       ///< +0x40
        hkArray<char>   m_nameUnlock;     ///< +0x50
        hkArray<hkReal> m_minPos;         ///< +0x60
        hkArray<hkReal> m_maxPos;         ///< +0x70
        hkArray<hkReal> m_minVel;         ///< +0x80
        hkArray<hkReal> m_maxVel;         ///< +0x90
        hkArray<hkReal> m_allBonesDown;   ///< +0xa0
        hkArray<hkReal> m_anyBonesDown;   ///< +0xb0
        hkReal          m_posTol;         ///< +0xc0
        hkReal          m_velTol;         ///< +0xc4
        hkReal          m_duration;       ///< +0xc8

        hkaFootstepAnalysisInfo(hkFinishLoadedObjectFlag flag);
        virtual ~hkaFootstepAnalysisInfo();
    };

    struct hkaFootstepAnalysisInfoContainer : hkReferencedObject
    {
        hkArray<hkRefPtr<hkaFootstepAnalysisInfo>> m_previewInfo;   ///< +0x10

        HK_FORCE_INLINE hkaFootstepAnalysisInfoContainer(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag), m_previewInfo(flag) {}
        virtual ~hkaFootstepAnalysisInfoContainer();
    };
} }
