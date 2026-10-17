/// @file
/// @brief hkpProcessCollisionData / hkpProcessCollisionOutput: what a collision agent's processing writes out (new
///        contact points, a time of impact, potential contacts).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpConstraintOwner;

    /// @note Only what the inline constructor touches is declared: the owner (+0x08) and the time of impact's time
    ///       (+0x3030, between which lie the 256 in-place contact points). Not reflected.
    class hkpProcessCollisionData
    {
    public:
        hkUint8             m_pad00[0x08];
        hkpConstraintOwner* m_constraintOwner;   ///< +0x08
        hkUint8             m_pad10[0x3030 - 0x10];
        hkTime              m_toiTime;           ///< +0x3030 — hkpToiInfo::m_time
        hkUint8             m_pad3034[0x3050 - 0x3034];

        /// @note Leaves the first-free contact point unset, as vanilla's inline copy does (no store to +0x00).
        HK_FORCE_INLINE hkpProcessCollisionData(hkpConstraintOwner* owner) : m_constraintOwner(owner) { m_toiTime = HK_REAL_MAX; }
    };

    /// @note The size is not known exactly: sized so the character controller's stack frame matches.
    class hkpProcessCollisionOutput : public hkpProcessCollisionData
    {
    public:
        void*   m_potentialContacts;   ///< +0x3050 — PotentialInfo*
        hkUint8 m_pad3058[0x30a0 - 0x3058];

        HK_FORCE_INLINE hkpProcessCollisionOutput(hkpConstraintOwner* owner) : hkpProcessCollisionData(owner) { m_potentialContacts = HK_NULL; }
    };
} }
