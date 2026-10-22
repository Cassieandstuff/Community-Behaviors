/// @file
/// @brief hkpWorldRayCastOutput: a world ray cast's hit, with the collidable it hit.
#pragma once

#include <Physics/Collide/Shape/Query/hkpShapeRayCastOutput.h>

namespace CB { namespace havok
{
    class hkpCollidable;

    /// @note 0x60 bytes.
    struct hkpWorldRayCastOutput : public hkpShapeRayCastOutput
    {
        const hkpCollidable* m_rootCollidable;   ///< +0x50 — null when nothing was hit

        HK_FORCE_INLINE hkpWorldRayCastOutput() : m_rootCollidable(HK_NULL) {}
        HK_FORCE_INLINE bool hasHit() const { return m_rootCollidable != HK_NULL; }
        HK_FORCE_INLINE void reset()
        {
            hkpShapeRayCastOutput::reset();
            m_rootCollidable = HK_NULL;
        }
    };
} }
