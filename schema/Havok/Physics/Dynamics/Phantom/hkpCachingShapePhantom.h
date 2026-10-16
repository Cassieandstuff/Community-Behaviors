/// @file
/// @brief hkpCachingShapePhantom: a shape phantom that caches a collision agent per overlapping collidable.
#pragma once

#include <Physics/Dynamics/Phantom/hkpShapePhantom.h>

namespace CB { namespace schema
{
    /// @note 0x1c0 bytes (the size its allocations use); vtable 0x141974af8.
    class hkpCachingShapePhantom : public hkpShapePhantom
    {
    public:
        struct CollisionDetail
        {
            void*          m_agent;        ///< +0x00 — hkpCollisionAgent*
            hkpCollidable* m_collidable;   ///< +0x08
        };

        hkArray<CollisionDetail> m_collisionDetails;   ///< +0x1a0
        hkBool                   m_orderDirty;         ///< +0x1b0

        /// @note Out of line in vanilla (0x140b49660).
        hkpCachingShapePhantom(const hkpShape* shape, const hkTransform& transform, hkUint32 collisionFilterInfo = 0);
    };
} }
