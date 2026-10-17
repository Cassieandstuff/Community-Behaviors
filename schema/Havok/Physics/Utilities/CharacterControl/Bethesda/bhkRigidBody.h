/// @file
/// @brief bhkWorldObject, bhkEntity and bhkRigidBody: Bethesda's wrappers of an hkpWorldObject, hkpEntity and
///        hkpRigidBody.
/// @note Not matched here: declared only as far as the character controller's code builds a bhkRigidBody.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkSerializable.h>
#include <Physics/Utilities/CharacterControl/Bethesda/BSTArray.h>

namespace CB { namespace schema
{
    class hkpAction;

    /// @note 0x28 bytes.
    class bhkWorldObject : public bhkSerializable
    {
    public:
        /// @note Out of line in vanilla (0x140e82130).
        bhkWorldObject();

        hkUint8 m_pad20[0x28 - 0x20];
    };

    /// @note 0x28 bytes: no members of its own. Vtable 0x1417adae0.
    class bhkEntity : public bhkWorldObject
    {
    public:
        HK_FORCE_INLINE bhkEntity() { ++s_instanceCount; }

        /// @brief How many have been constructed.
        /// @note Name provisional; vanilla bumps it (not atomically) in the constructor.
        static hkUint32 s_instanceCount;
    };

    /// @note 0x40 bytes. Vtable 0x1417aeb58.
    class bhkRigidBody : public bhkEntity
    {
    public:
        HK_FORCE_INLINE bhkRigidBody() { ++s_instanceCount; }

        virtual void CreateFromCinfo(hkSerializableCinfo* cinfo);   ///< slot 46
        virtual hkSerializableCinfo* GetCinfo(bool& created);       ///< slot 47

        /// @brief The actions attached to the body.
        BSTArray<hkRefPtr<hkpAction> > m_actions;   ///< +0x28

        /// @brief How many have been constructed.
        /// @note Name provisional; vanilla bumps it (not atomically) in the constructor.
        static hkUint32 s_instanceCount;
    };
} }
