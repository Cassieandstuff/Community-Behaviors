/// @file
/// @brief hkpAabbPhantom: a phantom that is just an AABB; ray casts against what overlaps it.
#pragma once

#include <Physics/Dynamics/Phantom/hkpPhantom.h>
#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>

namespace CB { namespace havok
{
    class hkpRayHitCollector;
    struct hkpWorldRayCastInput;

    /// @note vtable 0x14196c158.
    class hkpAabbPhantom : public hkpPhantom
    {
    public:
        hkAabb                  m_aabb;                     ///< +0xf0
        hkArray<hkpCollidable*> m_overlappingCollidables;   ///< +0x110
        hkBool                  m_orderDirty;               ///< +0x120

        /// @brief Moves the phantom to `newAabb` and updates the broad phase.
        void setAabb(const hkAabb& newAabb);
        /// @brief Casts `input`'s ray against the collidables overlapping the phantom.
        void castRay(const hkpWorldRayCastInput& input, hkpRayHitCollector& collector) const;

        HK_FORCE_INLINE const hkAabb& getAabb() const { return m_aabb; }
    };
} }
