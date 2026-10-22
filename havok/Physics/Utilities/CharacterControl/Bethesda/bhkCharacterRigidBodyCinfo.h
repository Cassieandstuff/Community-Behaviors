/// @file
/// @brief bhkCharacterRigidBodyCinfo: the construction info a bhkCharacterRigidBody builds its hkpCharacterRigidBody
///        from.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkSerializable.h>
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBodyCinfo.h>

namespace CB { namespace havok
{
    /// @note 0x80 bytes: the Havok cinfo at +0x00, the (empty) hkSerializableCinfo at +0x80. Vtable 0x1419ee430.
    class bhkCharacterRigidBodyCinfo : public hkSerializableCinfo, public hkpCharacterRigidBodyCinfo
    {
    public:
        using hkReferencedObject::operator new;
        using hkReferencedObject::operator delete;
        /// @brief Placement new as Havok's own (nothing assumed about the target): Bethesda tests a scrap-heap block
        ///        before constructing in it, and the cast to the hkSerializableCinfo base keeps its own null test.
        HK_FORCE_INLINE void* operator new(hkUlong, void* p) { return p; }
        HK_FORCE_INLINE void  operator delete(void*, void*) {}
    };
} }
