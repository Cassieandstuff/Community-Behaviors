/// @file
/// @brief hkaDefaultAnimatedReferenceFrame: extracted motion sampled at regular frames (translation + angle about up).
#pragma once

#include <Animation/Motion/hkaAnimatedReferenceFrame.h>

namespace CB { namespace schema
{
    struct hkaDefaultAnimatedReferenceFrame : hkaAnimatedReferenceFrame
    {
        hkVector4          m_up;                      ///< +0x10
        hkVector4          m_forward;                 ///< +0x20
        hkReal             m_duration;                ///< +0x30
        /// @brief One per frame: xyz the translation, w the angle about m_up.
        hkArray<hkVector4> m_referenceFrameSamples;   ///< +0x38

        HK_FORCE_INLINE hkaDefaultAnimatedReferenceFrame(hkFinishLoadedObjectFlag flag)
        :   hkaAnimatedReferenceFrame(flag), m_referenceFrameSamples(flag) {}

        /// @brief The sampled motion at `time`, interpolated between frames and clamped to the ends.
        virtual void   getReferenceFrame(hkReal time, hkQsTransform& motionOut) const;   ///< slot 3
        virtual void   getDeltaReferenceFrame(hkReal time, hkReal nextTime, int loops, hkQsTransform& deltaMotionOut, hkReal cropStartAmount,
                                              hkReal cropEndAmount) const;   ///< slot 4
        virtual hkReal getDuration() const;   ///< slot 5
    };
} }
