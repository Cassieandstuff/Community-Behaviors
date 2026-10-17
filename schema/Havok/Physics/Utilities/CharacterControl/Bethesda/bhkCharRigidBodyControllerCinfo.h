/// @file
/// @brief bhkCharRigidBodyControllerCinfo: the construction info of a bhkCharRigidBodyController.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterRigidBodyCinfo.h>

namespace CB { namespace schema
{
    /// @note 0x130 bytes; vtable 0x1419ee630.
    class bhkCharRigidBodyControllerCinfo : public bhkCharacterControllerCinfo
    {
    public:
        bhkCharRigidBodyControllerCinfo();
        /// @note Inline: vanilla has no copy of its own, only the deleting destructor it is inlined into.
        virtual ~bhkCharRigidBodyControllerCinfo() {}

        virtual void  SetPosition(const hkVector4& position);
        virtual float Unk_02() const;
        virtual float Unk_03() const;
        virtual void  Unk_04(float value);
        virtual float Unk_05() const;

        /// @brief What the controller's bhkCharacterRigidBody is built from (Init fills the shape, filter and up).
        bhkCharacterRigidBodyCinfo m_rigidBodyCinfo;   ///< +0xb0
    };
} }
