/// @file
/// @brief bhkCharacterRigidBody, reimplemented against vanilla (AE 1.6.1170).
/// @note Bethesda's engine code: built with C++ exceptions on and /Ob2, which the manifest sets on this unit. Every
///       allocating function files its allocations under memory context 0x36.
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterRigidBody.h>
#include <Physics/Utilities/CharacterControl/Bethesda/ahkpCharacterRigidBody.h>

namespace CB { namespace schema
{
    NiObject* bhkCharacterRigidBody::CreateObject()
    {
        MemoryContextScope context(0x36);
        return new bhkCharacterRigidBody();
    }

    bhkCharacterRigidBody::~bhkCharacterRigidBody()
    {
        bhkWorldLock lock(GetHavokWorld(), 1);
        Unk_30();
        --s_instanceCount;
    }

    void bhkCharacterRigidBody::LoadBinary(NiStream& stream)
    {
        stream.ReadLinkID();
        bhkSerializable::LoadBinary(stream);
    }

    void bhkCharacterRigidBody::LinkObject(NiStream& stream) { bhkSerializable::LinkObject(stream); }

    bool bhkCharacterRigidBody::RegisterStreamables(NiStream& stream) { return bhkSerializable::RegisterStreamables(stream); }

    void bhkCharacterRigidBody::SaveBinary(NiStream& stream) { bhkSerializable::SaveBinary(stream); }

    NiObject* bhkCharacterRigidBody::CreateClone(NiCloningProcess& cloning)
    {
        MemoryContextScope context(0x36);
        bhkCharacterRigidBody* clone = new bhkCharacterRigidBody();
        LockSerialize();
        CopyMembers(clone, cloning);
        UnlockSerialize();
        return clone;
    }

    void bhkCharacterRigidBody::AdjustRefCount(bool increment)
    {
        ahkpCharacterRigidBody* characterRB = static_cast<ahkpCharacterRigidBody*>(GetCharacterRigidBody());
        if (characterRB) characterRB->m_bhkCharacterRigidBody = increment ? this : HK_NULL;
        bhkSerializable::AdjustRefCount(increment);
    }

    hkpWorld* bhkCharacterRigidBody::GetWorld1()
    {
        hkpRigidBody* body = GetRigidBody();
        return body ? body->getWorld() : HK_NULL;
    }

    ahkpWorld* bhkCharacterRigidBody::GetWorld2() { return static_cast<ahkpWorld*>(GetWorld1()); }

    void bhkCharacterRigidBody::FreeCinfo(bool free)
    {
        MemoryContextScope context(0x36);
        if (free && m_cinfo) {
            bhkCharacterRigidBodyCinfo* cinfo = static_cast<bhkCharacterRigidBodyCinfo*>(m_cinfo);
            cinfo->~bhkCharacterRigidBodyCinfo();
            MemoryManager::GetSingleton()->GetThreadScrapHeap()->Deallocate(cinfo);
            m_cinfo = HK_NULL;
        }
    }

    void bhkCharacterRigidBody::CreateFromCinfo(hkSerializableCinfo* cinfo)
    {
        MemoryContextScope context(0x36);
        const bhkCharacterRigidBodyCinfo& info = *static_cast<bhkCharacterRigidBodyCinfo*>(cinfo);
        ahkpCharacterRigidBody* characterRB = new ahkpCharacterRigidBody(info);
        SetReferencedObject(characterRB);
        characterRB->removeReference();
        Unk_31(cinfo);
    }

    hkSerializableCinfo* bhkCharacterRigidBody::GetCinfo(bool& created)
    {
        MemoryContextScope context(0x36);
        hkSerializableCinfo* const cinfo = m_cinfo;
        created = (cinfo == HK_NULL);
        if (!cinfo) {
            void* memory = MemoryManager::GetSingleton()->GetThreadScrapHeap()->Allocate(sizeof(bhkCharacterRigidBodyCinfo), 0x10);
            bhkCharacterRigidBodyCinfo* info = memory ? new (memory) bhkCharacterRigidBodyCinfo : HK_NULL;
            m_cinfo = info;
        }
        return m_cinfo;
    }
} }
