/// @file
/// @brief bhkCharacterProxyCinfo: the construction info a bhkCharacterProxy builds its hkpCharacterProxy from.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkSerializable.h>
#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxyCinfo.h>

namespace CB { namespace schema
{
    /// @note 0x90 bytes: the Havok cinfo at +0x00, the (empty) hkSerializableCinfo at +0x90. Vtable 0x1419eddc8.
    class bhkCharacterProxyCinfo : public hkSerializableCinfo, public hkpCharacterProxyCinfo
    {
    public:
        using hkReferencedObject::operator new;
        using hkReferencedObject::operator delete;
        HK_FORCE_INLINE void* operator new(hkUlong, void* p) { return p; }
        HK_FORCE_INLINE void  operator delete(void*, void*) {}
    };
} }
