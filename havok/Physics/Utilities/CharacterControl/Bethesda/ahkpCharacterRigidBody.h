/// @file
/// @brief ahkpCharacterRigidBody: Bethesda's hkpCharacterRigidBody, which points back at the bhkCharacterRigidBody
///        wrapping it.
#pragma once

#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBody.h>

namespace CB { namespace havok
{
    class bhkCharacterRigidBody;

    /// @note 0xa0 bytes; vtables 0x1419fb650, 0x1419fb688 (+0x10) and 0x1419fb6c0 (+0x18).
    class ahkpCharacterRigidBody : public hkpCharacterRigidBody
    {
    public:
        /// @note Skyrim is z-up: the controller's up is forced to +z whatever the cinfo says.
        HK_FORCE_INLINE explicit ahkpCharacterRigidBody(const hkpCharacterRigidBodyCinfo& info)
        :   hkpCharacterRigidBody(info),
            m_bhkCharacterRigidBody(HK_NULL)
        {
            m_up.set(0.0f, 0.0f, 1.0f);
        }

        /// @brief The bhkCharacterRigidBody wrapping this one while it holds a reference (bhkCharacterRigidBody's
        ///        AdjustRefCount sets and clears it).
        bhkCharacterRigidBody* m_bhkCharacterRigidBody;   ///< +0x90
    };
} }
