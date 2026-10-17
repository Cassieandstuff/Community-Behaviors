/// @file
/// @brief bhkCharacterRigidBody: Bethesda's wrapper of an hkpCharacterRigidBody.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBody.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterRigidBodyCinfo.h>

namespace CB { namespace schema
{
    /// @note 0x20 bytes (vanilla's deleting destructor frees 0x20): no members of its own. Vtable 0x1419ee450.
    class bhkCharacterRigidBody : public bhkSerializable
    {
    public:
        HK_FORCE_INLINE bhkCharacterRigidBody() { ++s_instanceCount; }
        /// @note Out of line in vanilla (0x140f19310).
        virtual ~bhkCharacterRigidBody();

        /// @note Inline, as vanilla's: its one copy sits with the controller unit that emits the vtable.
        virtual const NiRTTI* GetRTTI() const { return &ms_RTTI; }      ///< slot 2
        virtual NiObject*     CreateClone(NiCloningProcess& cloning);   ///< slot 23
        virtual void          LoadBinary(NiStream& stream);             ///< slot 24
        virtual void          LinkObject(NiStream& stream);             ///< slot 25
        virtual bool          RegisterStreamables(NiStream& stream);    ///< slot 26
        virtual void          SaveBinary(NiStream& stream);             ///< slot 27
        virtual void          AdjustRefCount(bool increment);           ///< slot 38
        virtual hkpWorld*     GetWorld1();                              ///< slot 39
        virtual ahkpWorld*    GetWorld2();                              ///< slot 40
        virtual void          FreeCinfo(bool free);                     ///< slot 43
        /// @note Inline, as GetRTTI.
        virtual hkUint32      GetCinfoSize();                           ///< slot 44
        virtual void          CreateFromCinfo(hkSerializableCinfo* cinfo); ///< slot 46
        virtual hkSerializableCinfo* GetCinfo(bool& created);           ///< slot 47

        /// @brief The NiStream factory: a fresh, empty one.
        static NiObject* CreateObject();

        /// @brief GetWorld1, dispatched through the vtable.
        /// @note Name provisional. The destructor calls it through this inline (vanilla's call goes through the vtable,
        ///       where a direct call from the destructor would be bound statically).
        HK_FORCE_INLINE hkpWorld* GetHavokWorld() { return GetWorld1(); }

        /// @brief The class's Gamebryo RTTI record.
        static const NiRTTI ms_RTTI;
        /// @brief How many are alive.
        /// @note Name provisional; vanilla bumps it (not atomically) in the constructor and drops it in the destructor.
        static hkUint32 s_instanceCount;

        /// @brief The wrapped controller (the referenced object), null before one is built.
        HK_FORCE_INLINE hkpCharacterRigidBody* GetCharacterRigidBody() const
        {
            return static_cast<hkpCharacterRigidBody*>(m_referencedObject);
        }
        /// @brief The wrapped controller's body, null before one is built.
        HK_FORCE_INLINE hkpRigidBody* GetRigidBody() const
        {
            hkpCharacterRigidBody* characterRB = GetCharacterRigidBody();
            return characterRB ? characterRB->getRigidBody() : HK_NULL;
        }
        /// @brief The bhkWorld the wrapped controller's world belongs to, null if none.
        HK_FORCE_INLINE bhkWorld* GetBhkWorld()
        {
            hkpWorld* world = GetWorld1();
            return world ? static_cast<ahkpWorld*>(world)->m_bhkWorld : HK_NULL;
        }
        /// @brief Makes `listener` the wrapped controller's listener, under the world's write lock.
        HK_FORCE_INLINE void SetListener(hkpCharacterRigidBodyListener* listener)
        {
            hkpCharacterRigidBody* characterRB = GetCharacterRigidBody();
            if (characterRB) {
                bhkWorldLock lock(GetBhkWorld(), 1);
                characterRB->setListener(listener);
            }
        }
        /// @brief Runs the wrapped controller's support query under the world's read lock.
        /// @pre A controller has been built (no null check).
        HK_FORCE_INLINE void CheckSupport(const hkStepInfo& stepInfo, hkpSurfaceInfo& ground)
        {
            hkpCharacterRigidBody* characterRB = GetCharacterRigidBody();
            bhkWorldLock lock(GetBhkWorld(), 0);
            characterRB->checkSupport(stepInfo, ground);
        }
        /// @brief Teleports the wrapped controller's body to `position`, under the world's write lock.
        HK_FORCE_INLINE void SetPosition(const hkVector4& position)
        {
            hkpCharacterRigidBody* characterRB = GetCharacterRigidBody();
            if (characterRB) {
                bhkWorldLock lock(GetBhkWorld(), 1);
                characterRB->getRigidBody()->setPosition(position);
            }
        }
        /// @brief Drops the wrapped controller's listener, if it has one, under the world's write lock.
        HK_FORCE_INLINE void RemoveListener()
        {
            hkpCharacterRigidBody* characterRB = GetCharacterRigidBody();
            if (characterRB) {
                bhkWorldLock lock(GetBhkWorld(), 1);
                if (characterRB->m_listener) characterRB->removeListener();
            }
        }
    };

    inline hkUint32 bhkCharacterRigidBody::GetCinfoSize() { return sizeof(bhkCharacterRigidBodyCinfo); }
} }
