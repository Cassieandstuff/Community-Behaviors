/// @file
/// @brief hkpShapeRayCastCollectorOutput / hkpShapeRayCastOutput: what a ray cast reports about one hit.
#pragma once

#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes.
    struct hkpShapeRayCastCollectorOutput
    {
        hkVector4 m_normal;        ///< +0x00
        hkReal    m_hitFraction;   ///< +0x10 — 1 when nothing was hit
        int       m_extraInfo;     ///< +0x14
        /// @note m_pad[0] is reset to -1 with m_extraInfo (vanilla merges the two into one qword store).
        int       m_pad[2];        ///< +0x18

        HK_FORCE_INLINE hkpShapeRayCastCollectorOutput() { reset(); }
        HK_FORCE_INLINE void reset()
        {
            m_hitFraction = 1.0f;
            m_extraInfo   = -1;
            m_pad[0]      = -1;
        }
        HK_FORCE_INLINE bool hasHit() const { return m_hitFraction < 1.0f; }
    };

    /// @note 0x50 bytes: the hit plus the shape-key path down to the leaf shape.
    struct hkpShapeRayCastOutput : public hkpShapeRayCastCollectorOutput
    {
        enum { MAX_HIERARCHY_DEPTH = 8 };

        hkpShapeKey m_shapeKeys[MAX_HIERARCHY_DEPTH];   ///< +0x20
        int         m_shapeKeyIndex;                    ///< +0x40

        HK_FORCE_INLINE hkpShapeRayCastOutput() { reset(); }
        HK_FORCE_INLINE void reset()
        {
            hkpShapeRayCastCollectorOutput::reset();
            m_shapeKeys[0]  = HK_INVALID_SHAPE_KEY;
            m_shapeKeyIndex = 0;
        }
    };
} }
