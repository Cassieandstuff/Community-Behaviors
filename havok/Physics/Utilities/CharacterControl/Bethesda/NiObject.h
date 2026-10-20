/// @file
/// @brief NiObject: Gamebryo's streamable object, the root of Bethesda's bhk wrappers.
/// @note Not Havok: declared only as its vtable needs to be (slots per Gamebryo, as CommonLibSSE names them; none of
///       them is matched here).
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/NiRefObject.h>
#include <Physics/Utilities/CharacterControl/Bethesda/MemoryManager.h>

namespace CB { namespace havok
{
    class NiRTTI;
    /// @brief Gamebryo's object stream (NIF reader/writer).
    /// @note Opaque.
    class NiStream
    {
    public:
        /// @brief Reads one link (a block index) and queues it for the link pass.
        /// @note Name provisional.
        void ReadLinkID();
    };
    class NiCloningProcess;
    class NiObjectGroup;

    /// @note 0x10 bytes: no members of its own.
    class NiObject : public NiRefObject
    {
    public:
        /// @brief Gamebryo objects come from the engine heap (unaligned); they have no class operator delete, so a
        ///        deleting destructor frees through the global one.
        HK_FORCE_INLINE void* operator new(hkUlong nbytes) { return MemoryManager::GetSingleton()->Allocate(nbytes, 0, false); }

        virtual const NiRTTI* GetRTTI() const;                          ///< slot 2
        virtual NiObject*     AsNode();                                 ///< slot 3
        virtual NiObject*     AsSwitchNode();                           ///< slot 4
        virtual NiObject*     AsFadeNode();                             ///< slot 5
        virtual NiObject*     AsMultiBoundNode();                       ///< slot 6
        virtual NiObject*     AsGeometry();                             ///< slot 7
        virtual NiObject*     AsTriStrips();                            ///< slot 8
        virtual NiObject*     AsTriShape();                             ///< slot 9
        virtual NiObject*     AsSegmentedTriShape();                    ///< slot 10
        virtual NiObject*     AsSubIndexTriShape();                     ///< slot 11
        virtual NiObject*     AsDynamicTriShape();                      ///< slot 12
        virtual NiObject*     AsNiGeometry();                           ///< slot 13
        virtual NiObject*     AsNiTriBasedGeom();                       ///< slot 14
        virtual NiObject*     AsNiTriShape();                           ///< slot 15
        virtual NiObject*     AsParticlesGeom();                        ///< slot 16
        virtual NiObject*     AsLinesGeom();                            ///< slot 17
        virtual NiObject*     AsBhkNiCollisionObject();                 ///< slot 18
        virtual NiObject*     AsBhkBlendCollisionObject();              ///< slot 19
        virtual NiObject*     AsBhkAttachmentCollisionObject();         ///< slot 20
        virtual NiObject*     AsBhkRigidBody();                         ///< slot 21
        virtual NiObject*     AsBhkLimitedHingeConstraint();            ///< slot 22
        virtual NiObject*     CreateClone(NiCloningProcess& cloning);   ///< slot 23
        virtual void          LoadBinary(NiStream& stream);             ///< slot 24
        virtual void          LinkObject(NiStream& stream);             ///< slot 25
        virtual bool          RegisterStreamables(NiStream& stream);    ///< slot 26
        virtual void          SaveBinary(NiStream& stream);             ///< slot 27
        virtual bool          IsEqual(NiObject* object);                ///< slot 28
        virtual void          ProcessClone(NiCloningProcess& cloning);  ///< slot 29
        virtual void          PostLinkObject(NiStream& stream);         ///< slot 30
        virtual bool          StreamCanSkip();                          ///< slot 31
        virtual const NiRTTI* GetStreamableRTTI() const;                ///< slot 32
        virtual hkUint32      GetBlockAllocationSize() const;           ///< slot 33
        virtual NiObjectGroup* GetGroup() const;                        ///< slot 34
        virtual void          SetGroup(NiObjectGroup* group);           ///< slot 35
        virtual NiObject*     AsNiControllerManager();                  ///< slot 36
    };
} }
