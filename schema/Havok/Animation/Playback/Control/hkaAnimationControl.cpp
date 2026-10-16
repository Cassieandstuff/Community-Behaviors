/// @file
/// @brief hkaAnimationControl, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Playback/Control/hkaAnimationControl.h>

namespace CB { namespace schema
{
    hkaAnimationControl::hkaAnimationControl(const hkaAnimationBinding* binding)
    :   m_binding(binding)
    {
        m_motionTrackWeight = 1.0f;
        m_weight            = 1.0f;
        m_localTime         = 0.0f;
        if (binding) {
            m_transformTrackWeights.reserve(16);
            m_transformTrackWeights.setSize(m_binding->m_animation->m_numberOfTransformTracks, 0xff);
            m_floatTrackWeights.reserve(16);
            m_floatTrackWeights.setSize(m_binding->m_animation->m_numberOfFloatTracks, 0xff);
        }
    }

    hkaAnimationControl::~hkaAnimationControl()
    {
        for (int i = 0; i < m_listeners.getSize(); i++) m_listeners[i]->controlDeletedCallback(this);
    }

    void hkaAnimationControl::sampleTracks(hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        m_binding->m_animation->sampleTracks(m_localTime, transformTracksOut, floatTracksOut, cache);
    }

    void hkaAnimationControl::samplePartialTracks(hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                                  hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        m_binding->m_animation->samplePartialTracks(m_localTime, maxNumTransformTracks, transformTracksOut, maxNumFloatTracks, floatTracksOut, cache);
    }

    void hkaAnimationControl::setBinding(const hkaAnimationBinding* binding)
    {
        m_binding = binding;
        if (binding) {
            m_transformTrackWeights.reserve(16);
            m_transformTrackWeights.setSize(binding->m_animation->m_numberOfTransformTracks);
            m_floatTrackWeights.reserve(16);
            m_floatTrackWeights.setSize(binding->m_animation->m_numberOfFloatTracks);
            hkString::memSet(m_transformTrackWeights.begin(), 0xff, m_transformTrackWeights.getSize());
            hkString::memSet(m_floatTrackWeights.begin(), 0xff, m_floatTrackWeights.getSize());
        }
    }

    void hkaAnimationControl::addListener(hkaAnimationControlListener* listener)
    {
        m_listeners.pushBack(listener);
    }

    bool hkaAnimationControl::hasExtractedMotion() const
    {
        return m_binding && m_binding->m_animation->m_extractedMotion;
    }

    void hkaAnimationControl::getExtractedMotionDeltaReferenceFrame(hkReal deltaTime, hkQsTransform& deltaMotionOut) const
    {
        getExtractedMotionDeltaReferenceFrame(deltaTime, deltaMotionOut, 0.0f, 0.0f);
    }

    int hkaAnimationControl::getNumTransformTracks() const
    {
        return m_binding->m_animation->m_numberOfTransformTracks;
    }

    int hkaAnimationControl::getNumFloatTracks() const
    {
        return m_binding->m_animation->m_numberOfFloatTracks;
    }

    int hkaAnimationControl::getNumTransformTrackToBoneIndices() const
    {
        return m_binding->m_transformTrackToBoneIndices.getSize();
    }

    const hkInt16* hkaAnimationControl::getTransformTrackToBoneIndices() const
    {
        return m_binding->m_transformTrackToBoneIndices.begin();
    }

    int hkaAnimationControl::getNumFloatTrackToFloatSlotIndices() const
    {
        return m_binding->m_floatTrackToFloatSlotIndices.getSize();
    }

    const hkInt16* hkaAnimationControl::getFloatTrackToFloatSlotIndices() const
    {
        return m_binding->m_floatTrackToFloatSlotIndices.begin();
    }

    void hkaAnimationControl::getExtractedMotionDeltaReferenceFrame(hkReal deltaTime, hkQsTransform& deltaMotionOut, hkReal cropStartAmount,
                                                                    hkReal cropEndAmount) const
    {
        if (!hasExtractedMotion()) {
            deltaMotionOut.setIdentity();
        } else {
            hkReal futureTime;
            int    loops;
            getFutureTime(deltaTime, futureTime, loops);
            m_binding->m_animation->getExtractedMotionDeltaReferenceFrame(m_localTime, futureTime, loops, deltaMotionOut, cropStartAmount, cropEndAmount);
        }
    }
} }
