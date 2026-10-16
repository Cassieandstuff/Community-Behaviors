/// @file
/// @brief hkaQuantizedAnimation.
/// @note Minimal declaration (the reflection unit's getVtable needs the class and its finish-loaded constructor); the
///       sampler itself is reimplemented with the rest of the class.
#pragma once

#include <Animation/Animation/hkaAnimation.h>

namespace CB { namespace schema
{
    struct hkaQuantizedAnimation : hkaAnimation
    {
        HK_FORCE_INLINE hkaQuantizedAnimation(hkFinishLoadedObjectFlag flag) : hkaAnimation(flag) {}

        virtual void sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 3
        virtual void samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 4
        virtual void sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkQsTransform* transformOut) const;   ///< slot 6
        virtual void sampleIndividualFloatTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkReal* out) const;                      ///< slot 7
        virtual int  getNumOriginalFrames() const;   ///< slot 8
    };
} }
