/// @file
/// @brief hkaQuantizedAnimation: every frame of every track quantized into one byte stream, sampled whole.
#pragma once

#include <Animation/Animation/hkaAnimation.h>
#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    /// @note The data stream opens with this header; the frames follow at m_frameDataOffset, m_frameSize bytes each.
    ///       Field names are provisional (read from the sampler's offsets).
    struct hkaQuantizedAnimationHeader
    {
        hkUint16 m_frameDataOffset;   ///< +0x00 — where frame 0 starts
        hkUint16 m_numBones;          ///< +0x02 — transform tracks
        hkUint16 m_numFloats;         ///< +0x04 — float tracks
        hkUint16 m_numFrames;         ///< +0x06
        hkReal   m_duration;          ///< +0x08
        hkUint16 m_pad0c[8];          ///< +0x0c
        hkUint16 m_frameSize;         ///< +0x1c — bytes per frame
    };

    /// @note Only whole-pose sampling: sampleIndividualTransformTracks / sampleIndividualFloatTracks do nothing.
    class hkaQuantizedAnimation : public hkaAnimation
    {
    public:
        hkArray<hkUint8>   m_data;       ///< +0x38 — hkaQuantizedAnimationHeader, then the frames
        hkUint32           m_endian;     ///< +0x48
        /// @note Not serialized: set at runtime, it supplies the reference pose for tracks the data leaves out.
        const hkaSkeleton* m_skeleton;   ///< +0x50

        HK_FORCE_INLINE hkaQuantizedAnimation(hkFinishLoadedObjectFlag flag) : hkaAnimation(flag), m_data(flag) {}

        /// @note User-declared, as vanilla's re-stamps the vtable (0x140be2160).
        virtual ~hkaQuantizedAnimation();   ///< slot 0

        /// @brief Samples the whole pose, then writes out only the tracks with a positive weight, packed.
        virtual void sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, void* cache) const;   ///< slot 3
        /// @brief sampleTracks, stopping after `maxNumTransformTracks` / `maxNumFloatTracks` written tracks.
        virtual void samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, void* cache) const;   ///< slot 4
        /// @note Unsupported: does nothing.
        virtual void sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks,
                                                     hkQsTransform* transformOut) const;   ///< slot 6
        /// @note Unsupported: does nothing.
        virtual void sampleIndividualFloatTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkReal* out) const;   ///< slot 7
        virtual int  getNumOriginalFrames() const;   ///< slot 8

        /// @brief The whole pose and every float at `time`, with each track's weight.
        /// @note Name provisional; `additive` and `weightThreshold` are passed through to the static sampler (their
        ///       meaning is provisional too).
        void sampleFullPose(hkQsTransform* poseOut, hkReal* floatsOut, hkReal* poseWeightsOut, hkReal* floatWeightsOut, hkReal time, bool additive,
                            hkReal weightThreshold) const;

        /// @brief sampleFullPose over a raw data stream, with the skeleton's reference pose and floats for the tracks the
        ///        data leaves out; scratch buffers come from the temp allocator.
        /// @note Name provisional.
        static void sampleFullPose(const hkUint8* data, hkQsTransform* poseOut, hkReal* floatsOut, hkReal* poseWeightsOut, hkReal* floatWeightsOut,
                                   const hkQsTransform* referencePose, const hkReal* referenceFloats, hkReal time, bool additive,
                                   hkReal weightThreshold);
        /// @brief sampleFullPose with caller-supplied scratch: two pose buffers and two float buffers, each rounded up to a
        ///        multiple of four tracks.
        /// @note Name provisional; out of line in vanilla (0x140c31630).
        static void sampleFullPose(const hkUint8* data, hkQsTransform* poseOut, hkReal* floatsOut, hkReal* poseWeightsOut, hkReal* floatWeightsOut,
                                   const hkQsTransform* referencePose, const hkReal* referenceFloats, hkQsTransform* poseBufferA,
                                   hkQsTransform* poseBufferB, hkReal* floatBufferA, hkReal* floatBufferB, hkReal time, bool additive,
                                   hkReal weightThreshold);
    };
    static_assert(sizeof(hkaQuantizedAnimation) == 0x58, "hkaQuantizedAnimation size");
} }
