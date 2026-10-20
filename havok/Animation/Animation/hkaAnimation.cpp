/// @file
/// @brief hkaAnimation, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Animation/hkaAnimation.h>

namespace CB { namespace havok
{
    hkaAnimation::hkaAnimation(const hkaAnimation& other)
    :   hkReferencedObject(other)
    {
        m_type                    = other.m_type;
        m_duration                = other.m_duration;
        m_numberOfTransformTracks = other.m_numberOfTransformTracks;
        m_numberOfFloatTracks     = other.m_numberOfFloatTracks;
        m_extractedMotion         = other.m_extractedMotion;
        m_annotationTracks        = other.m_annotationTracks;
    }

    void hkaAnimation::clearAllCacheKeys(hkaChunkCache*) const
    {
    }

    hkUint32 hkaAnimation::getNumAnnotations(hkReal startTime, hkReal deltaTime) const
    {
        const hkReal endTime  = startTime + deltaTime;
        const hkReal wrapTime = (endTime > m_duration) ? endTime - m_duration : -HK_REAL_MAX;

        hkUint32 numAnnotations = 0;
        for (int i = 0; i < m_annotationTracks.getSize(); i++) {
            const hkaAnnotationTrack& track = m_annotationTracks[i];
            for (int j = 0; j < track.m_annotations.getSize(); j++) {
                const hkReal time = track.m_annotations[j].m_time;
                if ((time >= startTime && time < endTime) || time < wrapTime) numAnnotations++;
            }
        }
        return numAnnotations;
    }

    hkUint32 hkaAnimation::getAnnotations(hkReal startTime, hkReal deltaTime, TrackAnnotation* annotationsOut, hkUint32 maxAnnotations) const
    {
        if (maxAnnotations == 0) return 0;

        const hkReal endTime  = startTime + deltaTime;
        const hkReal wrapTime = (endTime > m_duration) ? endTime - m_duration : -HK_REAL_MAX;

        hkUint32  numAnnotations = 0;
        const int numTracks      = m_annotationTracks.getSize();
        for (hkUint16 i = 0; i < numTracks; i++) {
            const hkaAnnotationTrack& track      = m_annotationTracks[i];
            const int                 numInTrack = track.m_annotations.getSize();
            for (int j = 0; j < numInTrack; j++) {
                const hkReal time = track.m_annotations[j].m_time;
                if ((time >= startTime && time < endTime) || time < wrapTime) {
                    annotationsOut[numAnnotations].m_trackID    = i;
                    annotationsOut[numAnnotations].m_annotation = track.m_annotations[j];
                    numAnnotations++;
                    if (numAnnotations == maxAnnotations) return numAnnotations;
                }
            }
        }
        return numAnnotations;
    }
} }
