/// @file
/// @brief hkaAnimatedReferenceFrame: an animation's extracted (root) motion, sampled by time.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaAnimatedReferenceFrame : hkReferencedObject
    {
        HK_FORCE_INLINE hkaAnimatedReferenceFrame() {}
        HK_FORCE_INLINE hkaAnimatedReferenceFrame(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag) {}

        /// @brief The motion from time 0 to `time`.
        virtual void   getReferenceFrame(hkReal time, hkQsTransform& motionOut) const = 0;   ///< slot 3
        /// @brief The motion from `time` to `nextTime` across `loops` wraps of the animation cropped by the two amounts.
        virtual void   getDeltaReferenceFrame(hkReal time, hkReal nextTime, int loops, hkQsTransform& deltaMotionOut, hkReal cropStartAmount,
                                              hkReal cropEndAmount) const = 0;   ///< slot 4
        virtual hkReal getDuration() const = 0;   ///< slot 5
    };
} }
