/// @file
/// @brief hkaAnimation (the abstract animation) and its annotation tracks.
#pragma once

#include <Animation/Motion/hkaAnimatedReferenceFrame.h>
#include <Common/Base/System/Io/OStream/hkOStream.h>

namespace CB { namespace schema
{
    class hkaChunkCache;

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

        HK_FORCE_INLINE hkaAnnotationTrack() {}
        /// @note Out of line in vanilla (0x140c8aed0): hkaAnimation's copy constructor calls it.
        hkaAnnotationTrack(const hkaAnnotationTrack& other);
    };

    struct hkaAnimation : hkReferencedObject
    {
        enum AnimationType
        {
            HK_UNKNOWN_ANIMATION = 0,
            HK_INTERLEAVED_ANIMATION,
            HK_DELTA_COMPRESSED_ANIMATION,
            HK_WAVELET_COMPRESSED_ANIMATION,
            HK_MIRRORED_ANIMATION,
            HK_SPLINE_COMPRESSED_ANIMATION,
            HK_QUANTIZED_COMPRESSED_ANIMATION
        };

        /// @brief A block of compressed data a sampler streams in.
        struct DataChunk
        {
            const hkUint8* m_data;
            hkUint32       m_size;
            hkUint8        m_offset;
        };

        /// @brief An annotation and the track it came from (getAnnotations' output).
        /// @note 0x18 bytes.
        struct TrackAnnotation
        {
            hkUint16                       m_trackID;      ///< +0x00
            hkaAnnotationTrack::Annotation m_annotation;   ///< +0x08
        };

        hkInt32                             m_type;                      ///< +0x10, AnimationType
        hkReal                              m_duration;                  ///< +0x14
        hkInt32                             m_numberOfTransformTracks;   ///< +0x18
        hkInt32                             m_numberOfFloatTracks;       ///< +0x1c
        hkRefPtr<hkaAnimatedReferenceFrame> m_extractedMotion;           ///< +0x20
        hkArray<hkaAnnotationTrack>         m_annotationTracks;          ///< +0x28

        HK_FORCE_INLINE hkaAnimation(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag), m_extractedMotion(flag), m_annotationTracks(flag) {}
        hkaAnimation(const hkaAnimation& other);

        /// @brief Samples every track at `time`.
        virtual void sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const = 0;   ///< slot 3
        /// @brief Samples the first `maxNumTransformTracks` transform tracks and `maxNumFloatTracks` float tracks at `time`.
        /// @note The base raises an error: a sampler that supports it overrides it.
        virtual void samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 4
        /// @brief Drops this animation's entries from `cache`; the base keeps none.
        /// @note Name provisional.
        virtual void clearAllCacheKeys(hkaChunkCache* cache) const;   ///< slot 5
        virtual void sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkQsTransform* transformOut) const = 0;   ///< slot 6
        virtual void sampleIndividualFloatTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkReal* out) const = 0;                      ///< slot 7
        /// @note Havok's name: BSOffsetAnimationGenerator spaces its frames by duration / (frames - 1).
        virtual int  getNumOriginalFrames() const = 0;   ///< slot 8
        /// @brief How many data chunks sampling `frame` (+ `delta`) needs; the base streams none.
        virtual int  getNumDataChunks(hkUint32 frame, hkReal delta) const;   ///< slot 9
        /// @note The base raises an error.
        virtual void getDataChunks(hkUint32 frame, hkReal delta, DataChunk* dataChunks, int numDataChunks) const;   ///< slot 10
        virtual int  getMaxSizeOfCombinedDataChunks() const;   ///< slot 11
        /// @brief The extracted motion at `time` (m_extractedMotion's).
        virtual void getExtractedMotionReferenceFrame(hkReal time, hkQsTransform& motionOut) const;   ///< slot 12
        /// @brief The extracted motion from `time` to `nextTime` (m_extractedMotion's).
        virtual void getExtractedMotionDeltaReferenceFrame(hkReal time, hkReal nextTime, int loops, hkQsTransform& deltaMotionOut,
                                                           hkReal cropStartAmount, hkReal cropEndAmount) const;   ///< slot 13
        /// @brief The annotations in [startTime, startTime + deltaTime), wrapping past the end into [0, overrun).
        virtual hkUint32 getNumAnnotations(hkReal startTime, hkReal deltaTime) const;   ///< slot 14
        /// @brief Copies up to `maxAnnotations` of those annotations, track by track, into `annotationsOut`; how many.
        virtual hkUint32 getAnnotations(hkReal startTime, hkReal deltaTime, TrackAnnotation* annotationsOut, hkUint32 maxAnnotations) const;   ///< slot 15
    };

    /// @note hkaAnimation.inl: emitted (COMDAT) wherever the vtable is; the error ids and line numbers are vanilla's.
    inline void hkaAnimation::samplePartialTracks(hkReal, hkUint32, hkQsTransform*, hkUint32, hkReal*, hkaChunkCache*) const
    {
        char        buf[0x200];
        hkErrStream ostr(buf, sizeof(buf));
        ostr << "samplePartialPose not implemented for this type of animation";
        if (hkError::getInstance().message(hkError::MESSAGE_ERROR, 0x54e32123, buf,
                                           "C:\\Projects\\TESV\\Havok\\Source\\Animation/Animation/Animation/hkaAnimation.inl", 0x19)) {
            __debugbreak();
        }
    }

    inline int hkaAnimation::getNumDataChunks(hkUint32, hkReal) const
    {
        return 0;
    }

    inline void hkaAnimation::getDataChunks(hkUint32, hkReal, DataChunk*, int) const
    {
        char        buf[0x200];
        hkErrStream ostr(buf, sizeof(buf));
        ostr << "getDataChunks not implemented for this type of animation";
        if (hkError::getInstance().message(hkError::MESSAGE_ERROR, 0x54e32124, buf,
                                           "C:\\Projects\\TESV\\Havok\\Source\\Animation/Animation/Animation/hkaAnimation.inl", 0x25)) {
            __debugbreak();
        }
    }

    inline int hkaAnimation::getMaxSizeOfCombinedDataChunks() const
    {
        return 0;
    }

    inline void hkaAnimation::getExtractedMotionReferenceFrame(hkReal time, hkQsTransform& motionOut) const
    {
        m_extractedMotion->getReferenceFrame(time, motionOut);
    }

    inline void hkaAnimation::getExtractedMotionDeltaReferenceFrame(hkReal time, hkReal nextTime, int loops, hkQsTransform& deltaMotionOut,
                                                                    hkReal cropStartAmount, hkReal cropEndAmount) const
    {
        m_extractedMotion->getDeltaReferenceFrame(time, nextTime, loops, deltaMotionOut, cropStartAmount, cropEndAmount);
    }
} }
