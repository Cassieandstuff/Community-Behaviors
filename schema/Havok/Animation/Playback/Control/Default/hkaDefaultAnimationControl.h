/// @file
/// @brief hkaDefaultAnimationControl (looping playback with ease in/out and cropping) and its retargeting mapper data.
#pragma once

#include <Animation/Playback/Control/hkaAnimationControl.h>
#include <Animation/Mapper/hkaSkeletonMapper.h>

namespace CB { namespace schema
{
    struct hkaDefaultAnimationControl;

    /// @brief Told when a default control loops or finishes easing.
    struct hkaDefaultAnimationControlListener
    {
        virtual void loopOverflowCallback(hkaDefaultAnimationControl* control, hkReal deltaTime, hkUint32 overflows);     ///< slot 0
        virtual void loopUnderflowCallback(hkaDefaultAnimationControl* control, hkReal deltaTime, hkUint32 underflows);   ///< slot 1
        virtual void easedInCallback(hkaDefaultAnimationControl* control, hkReal deltaTime);                              ///< slot 2
        virtual void easedOutCallback(hkaDefaultAnimationControl* control, hkReal deltaTime);                             ///< slot 3
    };

    /// @brief What a control needs to play an animation bound to skeleton A on skeleton B.
    struct hkaDefaultAnimationControlMapperData : hkReferencedObject
    {
        /// @note The three tables' names are provisional, read off hkbClipGenerator::sampleAnimation: it passes the first
        ///       two to hkaSkeletonMapper::mapPoseLocalSpace and seeds the mapped pose from skeleton B's reference pose
        ///       through the third.
        /// @note A raw pointer the data holds a reference on (added in the constructor, released in the destructor).
        const hkaSkeletonMapper* m_mapper;                ///< +0x10
        hkArray<hkInt16>         m_boneToTrackIndicesA;   ///< +0x18
        hkArray<hkInt16>         m_boneToTrackIndicesB;   ///< +0x28
        hkArray<hkInt16>         m_trackToBoneIndicesB;   ///< +0x38

        hkaDefaultAnimationControlMapperData(const hkaSkeletonMapper* mapper, const hkaAnimationBinding* binding);
        virtual ~hkaDefaultAnimationControlMapperData();
    };

    struct hkaDefaultAnimationControl : hkaAnimationControl
    {
        enum EaseStatus { EASING_IN = 0, EASED_IN = 1, EASING_OUT = 2, EASED_OUT = 3 };

        hkReal     m_masterWeight;               ///< +0x58
        hkReal     m_playbackSpeed;              ///< +0x5c
        hkUint32   m_overflowCount;              ///< +0x60, loops played forward
        hkUint32   m_underflowCount;             ///< +0x64, loops played backward
        /// @brief Loops before playback stops at an end; 0 or less plays forever.
        int        m_maxCycles;                  ///< +0x68
        /// @brief The cubic Bézier control values of the ease-in / ease-out weight curves.
        hkVector4  m_easeInCurve;                ///< +0x70
        hkVector4  m_easeOutCurve;               ///< +0x80
        hkReal     m_easeInvDuration;            ///< +0x90
        /// @brief Progress through the current ease, 0..1.
        hkReal     m_easeT;                      ///< +0x94
        /// @note Unsigned in vanilla: its compares are `jbe`/`ja`.
        hkUint32   m_easeStatus;                 ///< +0x98, EaseStatus
        hkReal     m_cropStartAmountLocalTime;   ///< +0x9c
        hkReal     m_cropEndAmountLocalTime;     ///< +0xa0
        hkArray<hkaDefaultAnimationControlListener*> m_defaultListeners;   ///< +0xa8
        /// @note A raw pointer holding a reference (released when replaced and in the destructor).
        hkaDefaultAnimationControlMapperData*        m_mapperData;         ///< +0xb8

        /// @brief A control for `binding`, eased in (weight 1) or out (weight 0), stopping after `maxCycles` loops.
        hkaDefaultAnimationControl(const hkaAnimationBinding* binding, hkBool startEaseIn, hkInt32 maxCycles);
        /// @brief A copy of `other`'s playback state (not its listeners, nor its loop limit) on the same binding and
        ///        skeleton mapper.
        hkaDefaultAnimationControl(const hkaDefaultAnimationControl& other);
        virtual ~hkaDefaultAnimationControl();

        /// @brief Advances the local time by `stepDelta` × the playback speed (looping, cropped, limited to m_maxCycles)
        ///        and the ease, then sets the weight and tells the listeners.
        virtual void update(hkReal stepDelta);                                                    ///< slot 3
        virtual void getFutureTime(hkReal timestep, hkReal& localTimeOut, int& loopsOut) const;   ///< slot 4
        /// @brief With a skeleton mapper, samples into a scratch pose and maps it onto skeleton B's reference pose.
        virtual void sampleTracks(hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 5
        virtual void samplePartialTracks(hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 6
        /// @brief The cropped extracted motion, mapped onto skeleton B with a skeleton mapper.
        virtual void getExtractedMotionDeltaReferenceFrame(hkReal deltaTime, hkQsTransform& deltaMotionOut) const;   ///< slot 7
        virtual int            getNumTransformTracks() const;               ///< slot 8
        virtual int            getNumFloatTracks() const;                   ///< slot 9
        virtual int            getNumTransformTrackToBoneIndices() const;   ///< slot 10
        virtual const hkInt16* getTransformTrackToBoneIndices() const;      ///< slot 11

        /// @note Name provisional.
        const hkaSkeletonMapper* getSkeletonMapper() const;
        /// @brief Retargets through `mapper` (null: plays on the binding's own skeleton).
        /// @note Name provisional.
        void setSkeletonMapper(const hkaSkeletonMapper* mapper);

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
} }
