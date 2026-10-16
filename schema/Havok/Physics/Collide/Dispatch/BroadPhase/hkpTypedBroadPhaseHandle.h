/// @file
/// @brief hkpBroadPhaseHandle / hkpTypedBroadPhaseHandle: a collidable's broad-phase entry, its quality and filter info.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    class hkpBroadPhaseHandle
    {
    public:
        hkUint32 m_id;   ///< +0x00
    };

    /// @note 12 bytes (schema/physics/hkpTypedBroadPhaseHandle.yaml).
    class hkpTypedBroadPhaseHandle : public hkpBroadPhaseHandle
    {
    public:
        hkInt8   m_type;                  ///< +0x04 — hkpWorldObject::BroadPhaseType
        hkInt8   m_ownerOffset;           ///< +0x05
        hkInt8   m_objectQualityType;     ///< +0x06 — hkpCollidableQualityType
        hkUint32 m_collisionFilterInfo;   ///< +0x08

        HK_FORCE_INLINE hkInt8   getType() const { return m_type; }
        HK_FORCE_INLINE hkUint32 getCollisionFilterInfo() const { return m_collisionFilterInfo; }
        HK_FORCE_INLINE void     setCollisionFilterInfo(hkUint32 info) { m_collisionFilterInfo = info; }
    };
} }
