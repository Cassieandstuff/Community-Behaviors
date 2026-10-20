/// @file
/// @brief hkaDefaultAnimationControl and hkaDefaultAnimationControlMapperData, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Playback/Control/Default/hkaDefaultAnimationControl.h>

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace havok
{
    hkaDefaultAnimationControl::hkaDefaultAnimationControl(const hkaAnimationBinding* binding, hkBool startEaseIn, hkInt32 maxCycles)
    :   hkaAnimationControl(binding)
    {
        m_binding        = binding;
        m_masterWeight   = 1.0f;
        m_playbackSpeed  = 1.0f;
        m_overflowCount  = 0;
        m_underflowCount = 0;
        m_maxCycles      = maxCycles;
        m_mapperData     = HK_NULL;
        m_easeInCurve.set(0.0f, 0.0f, 1.0f, 1.0f);
        m_easeOutCurve.set(1.0f, 1.0f, 0.0f, 0.0f);
        if (startEaseIn) {
            m_easeStatus = EASED_IN;
            m_weight     = 1.0f;
        } else {
            m_easeStatus = EASED_OUT;
            m_weight     = 0.0f;
        }
        m_cropStartAmountLocalTime = 0.0f;
        m_cropEndAmountLocalTime   = 0.0f;
        m_easeInvDuration          = 10.0f;
        m_easeT                    = 1.0f;
    }

    hkaDefaultAnimationControl::hkaDefaultAnimationControl(const hkaDefaultAnimationControl& other)
    :   hkaAnimationControl(other.m_binding)
    {
        m_masterWeight             = other.m_masterWeight;
        m_playbackSpeed            = other.m_playbackSpeed;
        m_overflowCount            = other.m_overflowCount;
        m_underflowCount           = other.m_underflowCount;
        m_easeInCurve              = other.m_easeInCurve;
        m_easeOutCurve             = other.m_easeOutCurve;
        m_easeInvDuration          = other.m_easeInvDuration;
        m_easeT                    = other.m_easeT;
        m_easeStatus               = other.m_easeStatus;
        m_cropStartAmountLocalTime = other.m_cropStartAmountLocalTime;
        m_cropEndAmountLocalTime   = other.m_cropEndAmountLocalTime;
        setSkeletonMapper(other.getSkeletonMapper());
    }

    hkaDefaultAnimationControl::~hkaDefaultAnimationControl()
    {
        if (m_mapperData) m_mapperData->removeReference();
    }

    void hkaDefaultAnimationControl::update(hkReal stepDelta)
    {
        hkReal duration = HK_REAL_MAX;
        if (m_binding && m_binding->m_animation) {
            duration = hkMath::max2(m_binding->m_animation->m_duration - m_cropEndAmountLocalTime - m_cropStartAmountLocalTime, 0.0f);
        }

        m_localTime -= m_cropStartAmountLocalTime;
        if (m_localTime < 0.0f) m_localTime = 0.0f;

        if (duration != 0.0f) {
            int loops = 0;
            m_localTime += stepDelta * m_playbackSpeed;
            if (m_localTime > duration) {
                loops = static_cast<int>(m_localTime / duration);
                m_overflowCount += loops;
                m_localTime = hkMath::max2(m_localTime - static_cast<hkReal>(loops) * duration, 0.0f);
            } else if (m_localTime < 0.0f) {
                const int underflows = static_cast<int>(-(m_localTime / duration));
                loops                = -1 - underflows;
                m_underflowCount += underflows + 1;
                m_localTime = hkMath::min2(static_cast<hkReal>(underflows + 1) * duration + m_localTime, duration);
            }

            if (m_maxCycles > 0) {
                if (m_overflowCount >= hkUint32(m_maxCycles)) {
                    m_localTime     = duration;
                    m_playbackSpeed = 0.0f;
                } else if (m_underflowCount >= hkUint32(m_maxCycles + 1)) {
                    m_localTime     = 0.0f;
                    m_playbackSpeed = 0.0f;
                }
            }

            bool easeFinished = false;
            if (m_easeT < 1.0f) {
                m_easeT += hkMath::fabs(stepDelta) * m_easeInvDuration;
                if (m_easeT >= 1.0f) {
                    m_easeT      = 1.0f;
                    m_easeStatus = (m_easeStatus == EASING_IN) ? EASED_IN : EASED_OUT;
                    easeFinished = true;
                }
            }

            const hkVector4* curve = &m_easeOutCurve;
            switch (m_easeStatus) {
                case EASING_IN:
                case EASED_IN: curve = &m_easeInCurve; break;
            }
            const hkReal     t     = m_easeT;
            const hkReal     t2    = t * t;
            const hkReal     t3    = t2 * t;
            const hkReal     c0    = (*curve)(0);
            const hkReal     c1    = (*curve)(1);
            const hkReal     c2    = (*curve)(2);
            const hkReal     c3    = (*curve)(3);
            m_weight = m_masterWeight * ((-c0 + 3.0f * c1 - 3.0f * c2 + c3) * t3 + (3.0f * c0 - 6.0f * c1 + 3.0f * c2) * t2 + (-3.0f * c0 + 3.0f * c1) * t + c0);

            if (loops != 0) {
                for (int i = 0; i < m_defaultListeners.getSize(); i++) {
                    if (loops > 0) m_defaultListeners[i]->loopOverflowCallback(this, stepDelta, loops);
                    else           m_defaultListeners[i]->loopUnderflowCallback(this, stepDelta, -loops);
                }
            }
            if (easeFinished) {
                for (int i = 0; i < m_defaultListeners.getSize(); i++) {
                    if (m_easeStatus > EASED_IN) m_defaultListeners[i]->easedOutCallback(this, stepDelta);
                    else                         m_defaultListeners[i]->easedInCallback(this, stepDelta);
                }
            }
        }

        m_localTime += m_cropStartAmountLocalTime;
    }

    void hkaDefaultAnimationControl::getFutureTime(hkReal timestep, hkReal& localTimeOut, int& loopsOut) const
    {
        const hkReal cropStart = m_cropStartAmountLocalTime;
        const hkReal duration  = m_binding->m_animation->m_duration - m_cropEndAmountLocalTime - cropStart;
        if (duration <= 0.0f) {
            localTimeOut = m_localTime;
            loopsOut     = 0;
            return;
        }

        localTimeOut = m_localTime - cropStart + timestep * m_playbackSpeed;
        loopsOut     = 0;
        if (localTimeOut > duration) {
            loopsOut = static_cast<int>(localTimeOut / duration);
            if (m_maxCycles > 0 && m_overflowCount + loopsOut >= hkUint32(m_maxCycles)) {
                localTimeOut = duration;
                loopsOut     = m_maxCycles - m_overflowCount - 1;
            } else {
                localTimeOut -= static_cast<hkReal>(loopsOut) * duration;
            }
        } else if (localTimeOut < 0.0f) {
            loopsOut = static_cast<int>(localTimeOut / duration) - 1;
            if (m_maxCycles > 0 && m_underflowCount - loopsOut >= hkUint32(m_maxCycles + 1)) {
                localTimeOut = 0.0f;
                loopsOut     = m_maxCycles - m_underflowCount;
            } else {
                localTimeOut -= static_cast<hkReal>(loopsOut) * duration;
            }
        }
        localTimeOut += m_cropStartAmountLocalTime;
    }

    const hkaSkeletonMapper* hkaDefaultAnimationControl::getSkeletonMapper() const
    {
        return m_mapperData ? m_mapperData->m_mapper : HK_NULL;
    }

    void hkaDefaultAnimationControl::setSkeletonMapper(const hkaSkeletonMapper* mapper)
    {
        if (m_mapperData) m_mapperData->removeReference();
        if (mapper) {
            m_mapperData = new hkaDefaultAnimationControlMapperData(mapper, m_binding);
        } else {
            m_mapperData = HK_NULL;
        }
    }

    int hkaDefaultAnimationControl::getNumTransformTracks() const
    {
        if (!m_mapperData) return m_binding->m_animation->m_numberOfTransformTracks;
        return m_mapperData->m_trackToBoneIndicesB.getSize();
    }

    int hkaDefaultAnimationControl::getNumFloatTracks() const
    {
        return m_binding->m_animation->m_numberOfFloatTracks;
    }

    int hkaDefaultAnimationControl::getNumTransformTrackToBoneIndices() const
    {
        if (!m_mapperData) return m_binding->m_transformTrackToBoneIndices.getSize();
        return m_mapperData->m_trackToBoneIndicesB.getSize();
    }

    const hkInt16* hkaDefaultAnimationControl::getTransformTrackToBoneIndices() const
    {
        if (!m_mapperData) return m_binding->m_transformTrackToBoneIndices.begin();
        return m_mapperData->m_trackToBoneIndicesB.begin();
    }

    void hkaDefaultAnimationControl::sampleTracks(hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        const hkaAnimation* animation = m_binding->m_animation;
        if (!m_mapperData) {
            animation->sampleTracks(m_localTime, transformTracksOut, floatTracksOut, cache);
            return;
        }

        const int      numTracks = animation->m_numberOfTransformTracks;
        hkQsTransform* poseA     = hkAllocateStack<hkQsTransform>(numTracks);
        m_binding->m_animation->sampleTracks(m_localTime, poseA, floatTracksOut, cache);

        const hkaSkeleton* skeletonB = m_mapperData->m_mapper->m_mapping.m_skeletonB;
        const int          numBones  = m_mapperData->m_trackToBoneIndicesB.getSize();
        for (hkInt16 i = 0; i < numBones; i++) {
            transformTracksOut[i] = skeletonB->m_referencePose[m_mapperData->m_trackToBoneIndicesB[i]];
        }
        m_mapperData->m_mapper->mapPoseLocalSpace(poseA, transformTracksOut, m_mapperData->m_boneToTrackIndicesA.begin(),
                                                  m_mapperData->m_boneToTrackIndicesB.begin(), m_binding->m_blendHint == hkaAnimationBinding::ADDITIVE);
        hkDeallocateStack(poseA, numTracks);
    }

    void hkaDefaultAnimationControl::samplePartialTracks(hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                                         hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        m_binding->m_animation->samplePartialTracks(m_localTime, maxNumTransformTracks, transformTracksOut, maxNumFloatTracks, floatTracksOut, cache);
    }

    void hkaDefaultAnimationControl::getExtractedMotionDeltaReferenceFrame(hkReal deltaTime, hkQsTransform& deltaMotionOut) const
    {
        hkQsTransform motion;
        hkaAnimationControl::getExtractedMotionDeltaReferenceFrame(deltaTime, motion, m_cropStartAmountLocalTime, m_cropEndAmountLocalTime);
        if (!m_mapperData) {
            deltaMotionOut = motion;
        } else {
            m_mapperData->m_mapper->mapExtractedMotion(motion, deltaMotionOut);
        }
    }

    hkaDefaultAnimationControlMapperData::hkaDefaultAnimationControlMapperData(const hkaSkeletonMapper* mapper, const hkaAnimationBinding* binding)
    {
        m_mapper = mapper;
        mapper->addReference();
        mapper->computeTrackMappings(binding, m_boneToTrackIndicesA, m_boneToTrackIndicesB, m_trackToBoneIndicesB);
    }

    hkaDefaultAnimationControlMapperData::~hkaDefaultAnimationControlMapperData()
    {
        m_mapper->removeReference();
    }
} }
