/// @file
/// @brief hkaAnimation (the abstract animation) and its extracted-motion reference frame.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaAnimatedReferenceFrame : hkReferencedObject {};

    /// @brief One annotated track of an animation: its name and its timed text annotations.
    /// @note 0x18 bytes.
    struct hkaAnnotationTrack
    {
        /// @note 0x10 bytes.
        struct Annotation
        {
            hkReal      m_time;
            hkStringPtr m_text;
        };

        hkStringPtr         m_trackName;     ///< +0x00
        hkArray<Annotation> m_annotations;   ///< +0x08
    };

    struct hkaAnimation : hkReferencedObject
    {
        hkInt32                             m_type;                      ///< +0x10
        hkReal                              m_duration;                  ///< +0x14
        hkInt32                             m_numberOfTransformTracks;   ///< +0x18
        hkInt32                             m_numberOfFloatTracks;       ///< +0x1c
        hkRefPtr<hkaAnimatedReferenceFrame> m_extractedMotion;           ///< +0x20
        hkArray<hkaAnnotationTrack>         m_annotationTracks;          ///< +0x28

        /// @note Slots 3, 5, 7 and 9–12 are placeholders until the class is reimplemented.
        virtual void v3();
        /// @brief Samples the first `maxNumTransformTracks` transform tracks and `maxNumFloatTracks` float tracks at `time`.
        virtual void samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, void* cache) const;   ///< slot 4
        virtual void v5();
        virtual void sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks,
                                                     hkQsTransform* transformOut) const;   ///< slot 6
        virtual void v7();
        /// @note Havok's name: BSOffsetAnimationGenerator spaces its frames by duration / (frames - 1).
        virtual int  getNumOriginalFrames() const;   ///< slot 8
        virtual void v9();
        virtual void v10();
        virtual void v11();
        virtual void v12();
        virtual void getExtractedMotionDeltaReferenceFrame(hkReal time, hkReal nextTime, int loops, hkQsTransform& deltaMotionOut,
                                                           hkReal cropStartAmount, hkReal cropEndAmount) const;   ///< slot 13
    };
} }
