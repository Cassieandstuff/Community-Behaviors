/// @file
/// @brief hkpSimpleShapePhantom: a shape phantom that keeps a plain list of the collidables it overlaps.
#pragma once

#include <Physics/Dynamics/Phantom/hkpShapePhantom.h>

namespace CB { namespace schema
{
    /// @note 0x1c0 bytes (the size its allocations use); vtable 0x141974be8.
    class hkpSimpleShapePhantom : public hkpShapePhantom
    {
    public:
        struct CollisionDetail
        {
            hkpCollidable* m_collidable;
        };

        hkArray<CollisionDetail> m_collisionDetails;   ///< +0x1a0
        hkBool                   m_orderDirty;         ///< +0x1b0

        /// @note Out of line in vanilla (0x140b45840).
        hkpSimpleShapePhantom(const hkpShape* shape, const hkTransform& transform, hkUint32 collisionFilterInfo = 0);
    };
} }
