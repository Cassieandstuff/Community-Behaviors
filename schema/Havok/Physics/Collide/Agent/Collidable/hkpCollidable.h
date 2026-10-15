/// @file
/// @brief hkpCollidable: only what the behavior runtime reads (the physics reimplementation is a later pass).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note Layout per the Havok SDK: hkpCdBody (shape, shape key, motion, parent; 0x20), then the owner offset and the
    ///       typed broad-phase handle (id +0x24, type +0x28).
    class hkpCollidable
    {
    public:
        /// @note The Havok SDK's hkpWorldObject::BroadPhaseType values.
        enum BroadPhaseType { BROAD_PHASE_INVALID = 0, BROAD_PHASE_ENTITY = 1, BROAD_PHASE_PHANTOM = 2, BROAD_PHASE_BORDER = 3 };

        char    m_pad00[0x20];
        hkInt8  m_ownerOffset;          ///< +0x20 — the owning world object, relative to this
        char    m_pad21[0x28 - 0x21];
        hkInt8  m_broadPhaseType;       ///< +0x28 — m_broadPhaseHandle.m_type

        HK_FORCE_INLINE void*  getOwner() const { return const_cast<char*>(reinterpret_cast<const char*>(this)) + m_ownerOffset; }
        HK_FORCE_INLINE hkInt8 getType() const { return m_broadPhaseType; }
    };
} }
