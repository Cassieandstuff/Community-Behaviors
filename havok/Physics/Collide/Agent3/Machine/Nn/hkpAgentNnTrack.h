/// @file
/// @brief hkpAgentNnEntry: one collision agent of an n-squared agent track, linking a pair of collidables.
#pragma once

#include <Physics/Collide/Agent3/Machine/Nn/hkpLinkedCollidable.h>

namespace CB { namespace havok
{
    class hkpContactMgr;

    /// @note Only what hkpCharacterRigidBodyListener reads is declared: the contact manager and the collidable pair. The
    ///       agent header before them and the agent data after them are not. Not reflected; its size varies with the agent.
    struct hkpAgentNnEntry
    {
        hkUint8              m_pad00[0x08];
        hkpContactMgr*       m_contactMgr;      ///< +0x08
        hkUint8              m_pad10[0x18 - 0x10];
        hkpLinkedCollidable* m_collidable[2];   ///< +0x18

        HK_FORCE_INLINE hkpLinkedCollidable* getCollidableA() const { return m_collidable[0]; }
        HK_FORCE_INLINE hkpLinkedCollidable* getCollidableB() const { return m_collidable[1]; }
    };
} }
