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

        /// @note Slots 3–12 are placeholders until the class is reimplemented; only slot 13 is called so far.
        virtual void v3();
        virtual void v4();
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
