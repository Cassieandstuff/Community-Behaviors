/// @file
/// @brief hkaDefaultAnimatedReferenceFrame, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Motion/Default/hkaDefaultAnimatedReferenceFrame.h>

namespace CB { namespace schema
{
    void hkaDefaultAnimatedReferenceFrame::getReferenceFrame(hkReal time, hkQsTransform& motionOut) const
    {
        hkVector4 motion;
        const int numSamples = m_referenceFrameSamples.getSize();
        if (time >= m_duration) {
            motion = m_referenceFrameSamples[numSamples - 1];
        } else if (time < 0.0f) {
            motion = m_referenceFrameSamples[0];
        } else {
            const hkReal frameDuration = m_duration / static_cast<hkReal>(numSamples - 1);
            const hkReal frameReal     = time / frameDuration;
            int          frame         = static_cast<int>(frameReal);
            hkReal       delta         = frameReal - static_cast<hkReal>(frame);
            if (frame > numSamples - 2) {
                frame = numSamples - 2;
                delta = frameReal - static_cast<hkReal>(frame);
            }
            motion.setInterpolate4(m_referenceFrameSamples[frame], m_referenceFrameSamples[frame + 1], hkSimdReal(delta));
        }
        motionOut.m_translation = motion;
        motionOut.m_rotation.setAxisAngle(m_up, motion.getW());
        motionOut.m_scale.setAll(1.0f);
    }

    hkReal hkaDefaultAnimatedReferenceFrame::getDuration() const
    {
        return m_duration;
    }
} }
