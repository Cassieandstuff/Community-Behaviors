/// @file
/// @brief hkaSplineCompressedAnimation: tracks as quantized NURBS curves, cut into blocks of frames.
#pragma once

#include <Animation/Animation/hkaAnimation.h>

namespace CB { namespace schema
{
    /// @note A block holds, per transform track, a TransformMask (4 bytes), then per float track one mask byte, then
    ///       (at m_maskAndQuantizationSize) the curves themselves, each 4-aligned. m_floatBlockOffsets[b] is where block
    ///       b's float curves start, relative to the block.
    class hkaSplineCompressedAnimation : public hkaAnimation
    {
    public:
        /// @brief How a block's track is stored: which components are animated (spline), constant (static) or
        ///        identity, and with how many bits.
        struct TrackCompressionParams
        {
            enum RotationQuantization { POLAR32, THREECOMP40, THREECOMP48, THREECOMP24, STRAIGHT16, UNCOMPRESSED };
            enum ScalarQuantization { BITS8, BITS16 };
        };

        /// @brief One transform track's masks within a block.
        /// @note m_quantizationTypes packs the translation (bits 0–1), rotation (2–5) and scale (6–7) quantizations.
        struct TransformMask
        {
            hkUint8 m_quantizationTypes;   ///< +0x00
            hkUint8 m_positionTypes;       ///< +0x01
            hkUint8 m_rotationTypes;       ///< +0x02
            hkUint8 m_scaleTypes;          ///< +0x03
        };

        hkInt32           m_numFrames;                 ///< +0x38
        hkInt32           m_numBlocks;                 ///< +0x3c
        hkInt32           m_maxFramesPerBlock;         ///< +0x40
        hkInt32           m_maskAndQuantizationSize;   ///< +0x44 — where a block's curves start
        hkReal            m_blockDuration;             ///< +0x48
        hkReal            m_blockInverseDuration;      ///< +0x4c
        hkReal            m_frameDuration;             ///< +0x50
        hkArray<hkUint32> m_blockOffsets;              ///< +0x58 — into m_data
        hkArray<hkUint32> m_floatBlockOffsets;         ///< +0x68 — relative to the block
        hkArray<hkUint32> m_transformOffsets;          ///< +0x78 — per block and track, relative to the block
        hkArray<hkUint32> m_floatOffsets;              ///< +0x88 — per block and track, relative to the block
        hkArray<hkUint8>  m_data;                      ///< +0x98
        hkInt32           m_endian;                    ///< +0xa8

        HK_FORCE_INLINE hkaSplineCompressedAnimation(hkFinishLoadedObjectFlag flag)
        :   hkaAnimation(flag), m_blockOffsets(flag), m_floatBlockOffsets(flag), m_transformOffsets(flag), m_floatOffsets(flag), m_data(flag) {}

        /// @note User-declared, as vanilla's re-stamps the vtable (0x140c32e30).
        virtual ~hkaSplineCompressedAnimation();   ///< slot 0

        virtual void sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, void* cache) const;   ///< slot 3
        virtual void samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, void* cache) const;   ///< slot 4
        /// @note A track outside [0, m_numberOfTransformTracks) gets an all-zero transform.
        virtual void sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks,
                                                     hkQsTransform* transformOut) const;   ///< slot 6
        /// @note A track outside [0, m_numberOfFloatTracks) gets 0.
        virtual void sampleIndividualFloatTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkReal* out) const;   ///< slot 7
        virtual int  getNumOriginalFrames() const;   ///< slot 8
        /// @brief The object, the frame's block, and the float block offsets.
        virtual int  getNumDataChunks(hkUint32 frame, hkReal delta) const;   ///< slot 9
        /// @warning The third chunk is m_floatBlockOffsets from its start (one entry), whatever the block:
        ///          samplePartialWithDataChunks reads block 0's float offset for every block.
        virtual void getDataChunks(hkUint32 frame, hkReal delta, DataChunk* dataChunks, int numDataChunks) const;   ///< slot 10
        virtual int  getMaxSizeOfCombinedDataChunks() const;   ///< slot 11

        /// @brief The block `frame` falls in, the time within it, and that time quantized to the block's knot range.
        HK_FORCE_INLINE void getBlockAndTime(hkUint32 frame, hkReal delta, int& blockOut, hkReal& blockTimeOut, hkUint8& quantizedTimeOut) const;

        /// @brief Samples from getDataChunks' chunks.
        /// @pre dataChunks[0] is the animation object itself.
        static void samplePartialWithDataChunks(hkUint32 frameIndex, hkReal delta, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut,
                                                hkUint32 maxNumFloatTracks, hkReal* floatTracksOut, const DataChunk* dataChunks, int numDataChunks);

        /// @brief Where `offset` (its top bit ignored) lies in block `block`'s data.
        /// @note Out of line in vanilla (0x140c38450, compiled apart): behind Havok's product key-code check, it returns
        ///       null until the key code has validated.
        static const hkUint8* computePackedNurbsOffsets(const hkUint8* base, const hkUint32* blockOffsets, hkUint32 block, hkUint32 offset);

        /// @brief One transform track's translation, rotation or scale, or one float track, at the block time; the
        ///        data pointer advances past the curve, 4-aligned.
        /// @note Names provisional. Unanimated components take the identity's (0 for a float).
        void sampleTranslation(hkReal blockTime, hkUint8 quantizedTime, hkUint8 mask, TrackCompressionParams::ScalarQuantization quantization,
                               const hkUint8*& dataInOut, hkVector4& translationOut) const;
        void sampleRotation(hkReal blockTime, hkUint8 quantizedTime, hkUint8 mask, TrackCompressionParams::RotationQuantization quantization,
                            const hkUint8*& dataInOut, hkQuaternion& rotationOut) const;
        void sampleScale(hkReal blockTime, hkUint8 quantizedTime, hkUint8 mask, TrackCompressionParams::ScalarQuantization quantization,
                         const hkUint8*& dataInOut, hkVector4& scaleOut) const;
        void sampleFloat(hkReal blockTime, hkUint8 quantizedTime, hkUint8 mask, TrackCompressionParams::ScalarQuantization quantization,
                         const hkUint8*& dataInOut, hkReal& floatOut) const;

        /// @brief Reads one vector curve (`mask` picks the animated and static components; the rest come from
        ///        `defaultValue`) and evaluates it at `time`.
        /// @note Out of line in vanilla (0x140c33190).
        static void readNurbsCurve(const hkUint8*& dataInOut, hkUint8 quantizedTime, hkReal frameDuration, hkReal time, hkUint8 mask,
                                   TrackCompressionParams::ScalarQuantization quantization, const hkVector4& defaultValue, hkVector4& out);
        /// @brief Reads one rotation curve and evaluates it at `time`.
        /// @note Out of line in vanilla (0x140c336f0).
        static void readNurbsQuaternion(const hkUint8*& dataInOut, hkUint8 quantizedTime, hkReal frameDuration, hkReal time, hkUint8 mask,
                                        TrackCompressionParams::RotationQuantization quantization, hkQuaternion& out);
    };
    static_assert(sizeof(hkaSplineCompressedAnimation) == 0xb0, "hkaSplineCompressedAnimation size");

    HK_FORCE_INLINE void hkaSplineCompressedAnimation::getBlockAndTime(hkUint32 frame, hkReal delta, int& blockOut, hkReal& blockTimeOut,
                                                                       hkUint8& quantizedTimeOut) const
    {
        blockOut                          = frame / (m_maxFramesPerBlock - 1);
        blockOut                          = hkMath::max2(blockOut, 0);
        blockOut                          = hkMath::min2(blockOut, m_numBlocks - 1);
        const hkUint32 firstFrameOfBlock  = blockOut * (m_maxFramesPerBlock - 1);
        blockTimeOut                      = (static_cast<hkReal>(frame - firstFrameOfBlock) + delta) * m_frameDuration;
        quantizedTimeOut                  = static_cast<hkUint8>((blockTimeOut * m_blockInverseDuration) * (m_maxFramesPerBlock - 1));
    }
} }
