/// @file
/// @brief hkpCdBody: a shape placed in the world, linked to the body it is part of.
#pragma once

#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes (schema/physics/hkpCdBody.yaml).
    class hkpCdBody
    {
    public:
        const hkpShape*  m_shape;      ///< +0x00
        hkpShapeKey      m_shapeKey;   ///< +0x08
        /// @brief The root body's hkMotionState, a child's hkTransform: either way its transform comes first.
        const void*      m_motion;     ///< +0x10
        const hkpCdBody* m_parent;     ///< +0x18

        HK_FORCE_INLINE const hkpShape*    getShape() const { return m_shape; }
        HK_FORCE_INLINE hkpShapeKey        getShapeKey() const { return m_shapeKey; }
        HK_FORCE_INLINE const hkpCdBody*   getParent() const { return m_parent; }
        HK_FORCE_INLINE const hkTransform& getTransform() const { return *static_cast<const hkTransform*>(m_motion); }
        HK_FORCE_INLINE const void*        getMotionState() const { return m_motion; }
        /// @brief The top of the parent chain: the collidable a hit reports.
        HK_FORCE_INLINE const hkpCdBody*   getRootCdBody() const
        {
            const hkpCdBody* body = this;
            while (body->m_parent) body = body->m_parent;
            return body;
        }
    };
} }
