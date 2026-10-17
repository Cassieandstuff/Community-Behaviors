/// @file
/// @brief hkaQuantizedAnimation: every frame of every track quantized into one byte stream, sampled whole.
#pragma once

#include <Animation/Animation/hkaAnimation.h>
#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    /// @note The data stream opens with this header, then one weight byte per bone and per float track (indices into
    ///       hkaQuantizedAnimation::s_weightTable); the frames follow at m_frameDataOffset, m_frameSize bytes each. Static
    ///       elements (constant over the clip) are stored once: their component indices at m_staticIndicesOffset, their
    ///       values at m_staticValuesOffset. Dynamic ones are u16 per frame, decoded as q · scale + offset. A component
    ///       index counts hkReals into the pose (a rotation's counts hkVector4s). Field names are provisional (read from
    ///       the sampler's offsets); which of the two scalar counts is translation and which scale is not known.
    struct hkaQuantizedAnimationHeader
    {
        hkUint16 m_frameDataOffset;          ///< +0x00 — where frame 0 starts
        hkUint16 m_numBones;                 ///< +0x02 — transform tracks
        hkUint16 m_numFloats;                ///< +0x04 — float tracks
        hkUint16 m_numFrames;                ///< +0x06
        hkReal   m_duration;                 ///< +0x08
        hkUint16 m_numStaticTranslations;    ///< +0x0c — scalar components
        hkUint16 m_numStaticRotations;       ///< +0x0e
        hkUint16 m_numStaticScales;          ///< +0x10 — scalar components
        hkUint16 m_numStaticFloats;          ///< +0x12
        hkUint16 m_numDynamicTranslations;   ///< +0x14 — scalar components
        hkUint16 m_numDynamicRotations;      ///< +0x16
        hkUint16 m_numDynamicScales;         ///< +0x18 — scalar components
        hkUint16 m_numDynamicFloats;         ///< +0x1a
        hkUint16 m_frameSize;                ///< +0x1c — bytes per frame
        hkUint16 m_staticIndicesOffset;      ///< +0x1e
        hkUint16 m_dynamicIndicesOffset;     ///< +0x20
        hkUint16 m_staticValuesOffset;       ///< +0x22
        hkUint16 m_dynamicOffsetsOffset;     ///< +0x24
        hkUint16 m_dynamicScalesOffset;      ///< +0x26
    };
    static_assert(sizeof(hkaQuantizedAnimationHeader) == 0x28, "hkaQuantizedAnimationHeader size");

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
        virtual void sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 3
        /// @brief sampleTracks, stopping after `maxNumTransformTracks` / `maxNumFloatTracks` written tracks.
        virtual void samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 4
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
        /// @brief sampleFullPose with caller-supplied scratch (see the static overload).
        /// @note Name provisional; no code callers left in vanilla (0x140c30960).
        void sampleFullPose(hkQsTransform* poseOut, hkReal* floatsOut, hkReal* poseWeightsOut, hkReal* floatWeightsOut, hkQsTransform* poseBufferA,
                            hkQsTransform* poseBufferB, hkReal* floatBufferA, hkReal* floatBufferB, hkReal time, bool additive,
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
        /// @brief Decodes the frame pair `frameA`/`frameB` and blends it by `delta`: a delta within `weightThreshold` of 0
        ///        or 1 decodes only the nearer frame, straight into the output; otherwise both go into the scratch buffers
        ///        and are blended. Tracks the frames leave out keep the reference pose; the weights come from the header.
        /// @note Name provisional; out of line in vanilla (0x140c31770).
        static void sampleFrames(const hkUint8* data, const hkUint8* frameA, const hkUint8* frameB, hkQsTransform* poseOut, hkReal* floatsOut,
                                 hkReal* poseWeightsOut, hkReal* floatWeightsOut, const hkQsTransform* referencePose, const hkReal* referenceFloats,
                                 hkQsTransform* poseBufferA, hkQsTransform* poseBufferB, hkReal* floatBufferA, hkReal* floatBufferB, hkReal delta,
                                 bool additive, hkReal weightThreshold);

        /// @brief out[indices[i]] = values[i] for `count` components.
        /// @note Name provisional (0x140c31c30).
        static void scatterScalars(hkReal* out, const hkUint16* indices, const hkReal* values, int count);
        /// @brief Unpacks `count` 48-bit quaternions (three u16 each, four at a time) to out[indices[i]].
        /// @note Name provisional (0x140c31cf0).
        static void scatterQuaternions(hkVector4* out, const hkUint16* indices, const hkUint16* packed, int count);
        /// @brief out[indices[i]] = quantized[i] · scales[i] + offsets[i] for `count` components.
        /// @note Name provisional (0x140c31ce0): forwards to decodeDynamicScalars.
        static void sampleDynamicScalars(hkReal* out, const hkUint16* indices, const hkReal* offsets, const hkReal* scales, const hkUint16* quantized,
                                         int count);

        /// @note Out of line in another unit, behind Havok's key-code check (0x140c8b030). Name provisional.
        static void decodeDynamicScalars(hkReal* out, const hkUint16* indices, const hkReal* offsets, const hkReal* scales, const hkUint16* quantized,
                                         int count);
        /// @brief Unpacks four consecutive 48-bit quaternions.
        /// @note Out of line in another unit (0x140c974f0). Name provisional.
        static void unpackQuaternions4(hkVector4* q0, hkVector4* q1, hkVector4* q2, hkVector4* q3, const hkUint16* packed);
        /// @brief Blends `count` transforms (`floats`) of `a` toward `b` by `weight`.
        /// @note Out of line in another unit (0x140c8b380 / 0x140c8b290). Names and the `mode` argument (always 0 here)
        ///       provisional.
        static void blendPose(hkQsTransform* out, const hkQsTransform* a, const hkQsTransform* b, hkSimdReal weight, int count, int mode, bool additive);
        static void blendFloats(hkReal* out, const hkReal* a, const hkReal* b, hkSimdReal weight, int count, int mode);

        /// @brief A track's weight from its header weight byte.
        /// @note Name provisional (0x142024b90).
        static const hkReal s_weightTable[256];
    };
    static_assert(sizeof(hkaQuantizedAnimation) == 0x58, "hkaQuantizedAnimation size");
} }
