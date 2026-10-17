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

    /// @note Each wrap applies one whole cropped cycle (start → end) to the next-time motion: forward loops compose it on,
    ///       backward loops take it off.
    void hkaDefaultAnimatedReferenceFrame::getDeltaReferenceFrame(hkReal time, hkReal nextTime, int loops, hkQsTransform& deltaMotionOut,
                                                                  hkReal cropStartAmount, hkReal cropEndAmount) const
    {
        const hkReal cropEnd = m_duration - cropEndAmount;
        if (cropStartAmount >= cropEnd) {
            deltaMotionOut.setIdentity();
            return;
        }

        hkQsTransform motionNow;
        getReferenceFrame(time, motionNow);
        hkQsTransform motionNext;
        getReferenceFrame(nextTime, motionNext);

        if (loops != 0) {
            hkQsTransform motionEnd;
            getReferenceFrame(cropEnd, motionEnd);
            hkQsTransform cycleMotion;
            if (cropStartAmount != 0.0f) {
                hkQsTransform motionStart;
                getReferenceFrame(cropStartAmount, motionStart);
                cycleMotion.setMulInverseMul(motionStart, motionEnd);
            } else {
                cycleMotion = motionEnd;
            }

            const int numLoops = (loops < 0) ? -loops : loops;
            if (numLoops != 0) {
                const bool sameRotation = cycleMotion.m_rotation.m_vec.equals4(motionNext.m_rotation.m_vec, 1e-3f);
                for (int i = numLoops; i != 0; i--) {
                    if (loops < 0) {
                        motionNext.setMulInverseMul(cycleMotion, motionNext);
                    } else {
                        motionNext.setMul(cycleMotion, motionNext);
                        if (sameRotation) motionNext.m_rotation = cycleMotion.m_rotation;
                    }
                }
            }
        }

        deltaMotionOut.setMulInverseMul(motionNow, motionNext);
        deltaMotionOut.m_scale.setAll(1.0f);
    }

    hkReal hkaDefaultAnimatedReferenceFrame::getDuration() const
    {
        return m_duration;
    }
} }
