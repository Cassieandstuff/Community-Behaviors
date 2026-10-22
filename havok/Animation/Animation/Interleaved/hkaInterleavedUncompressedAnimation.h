/// @file
/// @brief hkaInterleavedUncompressedAnimation: every track's transform stored raw, frame after frame.
#pragma once

#include <Animation/Animation/hkaAnimation.h>

namespace CB { namespace havok
{
    /// @note Frame f's transforms are m_transforms[f × m_numberOfTransformTracks …], its floats likewise in m_floats.
    ///       The destructor is implicit (no vtable store in vanilla's 0x140bde9b0), calling ~hkaAnimation out of line.
    class hkaInterleavedUncompressedAnimation : public hkaAnimation
    {
    public:
        hkArray<hkQsTransform> m_transforms;   ///< +0x38
        hkArray<hkReal>        m_floats;       ///< +0x48

        HK_FORCE_INLINE hkaInterleavedUncompressedAnimation(hkFinishLoadedObjectFlag flag)
        :   hkaAnimation(flag), m_transforms(flag), m_floats(flag) {}
        hkaInterleavedUncompressedAnimation(const hkaInterleavedUncompressedAnimation& other);

        virtual void sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 3
        /// @note At or past the duration it copies the last frame (no interpolation).
        virtual void samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 4
        /// @note A track outside [0, m_numberOfTransformTracks) leaves its output untouched.
        virtual void sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks,
                                                     hkQsTransform* transformOut) const;   ///< slot 6
        virtual void sampleIndividualFloatTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkReal* out) const;   ///< slot 7
        virtual int  getNumOriginalFrames() const;   ///< slot 8
        /// @brief The object, then a frame pair of transforms and one of floats when the animation has them.
        virtual int  getNumDataChunks(hkUint32 frame, hkReal delta) const;   ///< slot 9
        /// @pre `numDataChunks` >= getNumDataChunks(frame, delta).
        virtual void getDataChunks(hkUint32 frame, hkReal delta, DataChunk* dataChunks, int numDataChunks) const;   ///< slot 10
        virtual int  getMaxSizeOfCombinedDataChunks() const;   ///< slot 11

        /// @brief Pre-multiplies every frame of `trackIndex` by `transform`.
        /// @note Name and signature provisional: vanilla's 0x140c30210 has no code callers left.
        void transformTrack(int trackIndex, const hkQsTransform& transform);

        /// @brief Samples from getDataChunks' chunks: the frame pair `frameIndex`/`frameIndex` + 1 blended by `delta`.
        /// @pre dataChunks[0] is the animation object itself.
        static void samplePartialWithDataChunks(hkUint32 frameIndex, hkReal delta, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut,
                                                hkUint32 maxNumFloatTracks, hkReal* floatTracksOut, const DataChunk* dataChunks, int numDataChunks);
    };
    static_assert(sizeof(hkaInterleavedUncompressedAnimation) == 0x58, "hkaInterleavedUncompressedAnimation size");
} }
