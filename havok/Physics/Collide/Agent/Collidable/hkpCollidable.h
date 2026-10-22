/// @file
/// @brief hkpCollidable: a world object's root collision body (shape, owner, broad-phase handle, AABB cache).
#pragma once

#include <Physics/Collide/Agent/Collidable/hkpCdBody.h>
#include <Physics/Collide/Dispatch/BroadPhase/hkpTypedBroadPhaseHandle.h>

namespace CB { namespace havok
{
    /// @brief How carefully a body's collisions are simulated (continuous physics, solver priority).
    /// @note Values per the Havok SDK; HK_COLLIDABLE_QUALITY_KEYFRAMED (1) and _MOVING (4) are confirmed by the attachment
    ///       modifier.
    enum hkpCollidableQualityType
    {
        HK_COLLIDABLE_QUALITY_INVALID            = -1,
        HK_COLLIDABLE_QUALITY_FIXED              = 0,
        HK_COLLIDABLE_QUALITY_KEYFRAMED          = 1,
        HK_COLLIDABLE_QUALITY_DEBRIS             = 2,
        HK_COLLIDABLE_QUALITY_DEBRIS_SIMPLE_TOI  = 3,
        HK_COLLIDABLE_QUALITY_MOVING             = 4,
        HK_COLLIDABLE_QUALITY_CRITICAL           = 5,
        HK_COLLIDABLE_QUALITY_BULLET             = 6,
        HK_COLLIDABLE_QUALITY_USER               = 7,
        HK_COLLIDABLE_QUALITY_CHARACTER          = 8,
        HK_COLLIDABLE_QUALITY_KEYFRAMED_REPORTING = 9,
    };

    /// @note 0x70 bytes (schema/physics/hkpCollidable.yaml).
    class hkpCollidable : public hkpCdBody
    {
    public:
        /// @note The Havok SDK's hkpWorldObject::BroadPhaseType values.
        enum BroadPhaseType { BROAD_PHASE_INVALID = 0, BROAD_PHASE_ENTITY = 1, BROAD_PHASE_PHANTOM = 2, BROAD_PHASE_BORDER = 3 };

        /// @brief The broad phase's cached, compressed AABB.
        /// @note 0x38 bytes.
        struct BoundingVolumeData
        {
            hkUint32     m_min[3];                    ///< +0x00
            hkUint8      m_expansionMin[3];           ///< +0x0c
            hkUint8      m_expansionShift;            ///< +0x0f
            hkUint32     m_max[3];                    ///< +0x10
            hkUint8      m_expansionMax[3];           ///< +0x1c
            hkUint8      m_padding;                   ///< +0x1f
            hkUint16     m_numChildShapeAabbs;        ///< +0x20
            hkUint16     m_capacityChildShapeAabbs;   ///< +0x22
            void*        m_childShapeAabbs;           ///< +0x28 — hkAabbUint32*
            hkpShapeKey* m_childShapeKeys;            ///< +0x30
        };

        hkInt8                   m_ownerOffset;               ///< +0x20 — the owning world object, relative to this
        hkInt8                   m_forceCollideOntoPpu;       ///< +0x21
        hkInt16                  m_shapeSizeOnSpu;            ///< +0x22
        hkpTypedBroadPhaseHandle m_broadPhaseHandle;          ///< +0x24
        BoundingVolumeData       m_boundingVolumeData;        ///< +0x30
        hkReal                   m_allowedPenetrationDepth;   ///< +0x68

        HK_FORCE_INLINE void*  getOwner() const { return const_cast<char*>(reinterpret_cast<const char*>(this)) + m_ownerOffset; }
        HK_FORCE_INLINE hkInt8 getType() const { return m_broadPhaseHandle.getType(); }

        HK_FORCE_INLINE const hkpTypedBroadPhaseHandle* getBroadPhaseHandle() const { return &m_broadPhaseHandle; }
        HK_FORCE_INLINE hkpTypedBroadPhaseHandle*       getBroadPhaseHandle() { return &m_broadPhaseHandle; }
        HK_FORCE_INLINE hkUint32 getCollisionFilterInfo() const { return m_broadPhaseHandle.getCollisionFilterInfo(); }
        HK_FORCE_INLINE void     setCollisionFilterInfo(hkUint32 info) { m_broadPhaseHandle.setCollisionFilterInfo(info); }
        HK_FORCE_INLINE hkpCollidableQualityType getQualityType() const { return static_cast<hkpCollidableQualityType>(m_broadPhaseHandle.m_objectQualityType); }
        HK_FORCE_INLINE void     setQualityType(hkpCollidableQualityType type) { m_broadPhaseHandle.m_objectQualityType = static_cast<hkInt8>(type); }
        HK_FORCE_INLINE hkReal   getAllowedPenetrationDepth() const { return m_allowedPenetrationDepth; }
    };
} }
