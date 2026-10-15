/// @file
/// @brief hkaAnimation (the abstract animation) and its extracted-motion reference frame.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaAnimatedReferenceFrame : hkReferencedObject {};

    struct hkaAnimation : hkReferencedObject
    {
        hkInt32                             m_type;                      ///< +0x10
        hkReal                              m_duration;                  ///< +0x14
        hkInt32                             m_numberOfTransformTracks;   ///< +0x18
        hkInt32                             m_numberOfFloatTracks;       ///< +0x1c
        hkRefPtr<hkaAnimatedReferenceFrame> m_extractedMotion;           ///< +0x20

        /// @note Slots 3 and 5–12 are placeholders until the class is reimplemented.
        virtual void v3();
        /// @brief Samples the first `maxNumTransformTracks` transform tracks and `maxNumFloatTracks` float tracks at `time`.
        virtual void samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, void* cache) const;   ///< slot 4
        virtual void v5();
        virtual void v6();
        virtual void v7();
        virtual void v8();
        virtual void v9();
        virtual void v10();
        virtual void v11();
        virtual void v12();
        virtual void getExtractedMotionDeltaReferenceFrame(hkReal time, hkReal nextTime, int loops, hkQsTransform& deltaMotionOut,
                                                           hkReal cropStartAmount, hkReal cropEndAmount) const;   ///< slot 13
    };
} }
