/// @file
/// @brief hkaAnimationControl, hkaDefaultAnimationControl and its retargeting mapper data.
#pragma once

#include <Animation/Animation/hkaAnimationBinding.h>
#include <Animation/Mapper/hkaSkeletonMapper.h>

namespace CB { namespace schema
{
    struct hkaAnimationControl : hkReferencedObject
    {
        hkReal                        m_localTime;   ///< +0x10
        char                          m_pad14[0x38 - 0x14];
        hkRefPtr<hkaAnimationBinding> m_binding;     ///< +0x38

        virtual void update(hkReal stepDelta);                                                    ///< slot 3
        virtual void getFutureTime(hkReal timestep, hkReal& localTimeOut, int& loopsOut) const;   ///< slot 4
    };

    struct hkaDefaultAnimationControl : hkaAnimationControl
    {
        char   m_pad40[0x5c - 0x40];
        hkReal m_playbackSpeed;              ///< +0x5c
        char   m_pad60[0x9c - 0x60];
        hkReal m_cropStartAmountLocalTime;   ///< +0x9c
        hkReal m_cropEndAmountLocalTime;     ///< +0xa0
        char   m_padA4[0xc0 - 0xa4];

        hkaDefaultAnimationControl(const hkaAnimationBinding* binding, hkBool ease, hkInt32 maxCycles);
        /// @note Name provisional.
        void setBinding(const hkaAnimationBinding* binding);

        HK_FORCE_INLINE void setCropStartAmountLocalTime(hkReal cropStart)
        {
            m_cropStartAmountLocalTime = cropStart;
            if (m_localTime < cropStart) m_localTime = cropStart;
        }
        /// @note The end is computed from the member, not the argument: that re-read is what sinks vanilla's load of
        ///       the crop past the binding checks.
        HK_FORCE_INLINE void setCropEndAmountLocalTime(hkReal cropEnd)
        {
            if (m_binding && m_binding->m_animation) {
                m_cropEndAmountLocalTime = cropEnd;
                const hkReal end         = m_binding->m_animation->m_duration - m_cropEndAmountLocalTime;
                if (m_localTime >= end) m_localTime = end;
            }
        }
    };

    struct hkaDefaultAnimationControlMapperData : hkReferencedObject
    {
        /// @note The three tables' names are provisional, read off hkbClipGenerator::sampleAnimation: it passes the first
        ///       two to hkaSkeletonMapper::mapPoseLocalSpace and seeds the mapped pose from skeleton B's reference pose
        ///       through the third.
        const hkaSkeletonMapper* m_mapper;                ///< +0x10
        hkArray<hkInt16>         m_boneToTrackIndicesA;   ///< +0x18
        hkArray<hkInt16>         m_boneToTrackIndicesB;   ///< +0x28
        hkArray<hkInt16>         m_trackToBoneIndicesB;   ///< +0x38

        hkaDefaultAnimationControlMapperData(const hkaSkeletonMapper* mapper, const hkaAnimationBinding* binding);
    };
} }
