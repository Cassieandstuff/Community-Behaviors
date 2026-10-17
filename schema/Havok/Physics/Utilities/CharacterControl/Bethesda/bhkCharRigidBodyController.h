/// @file
/// @brief bhkCharRigidBodyController: Bethesda's character controller over Havok's rigid-body controller (the one
///        Skyrim drives actors with, bUseCharacterRB). It is also the hkpCharacterRigidBody's listener.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharRigidBodyControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterRigidBody.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterPointCollector.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkRigidBody.h>
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBodyListener.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Common/Base/Types/Color/hkColor.h>

namespace CB { namespace schema
{
    /// @note 0x5b0 bytes; vtables 0x1419ee668 and, for the listener (+0x330), 0x1419ee718.
    class bhkCharRigidBodyController : public bhkCharacterController, public hkpCharacterRigidBodyListener
    {
    public:
        using bhkCharacterController::operator new;
        using bhkCharacterController::operator delete;

        /// @brief Builds the controller from `cinfo` (which Init completes and passes on to the Havok body).
        explicit bhkCharRigidBodyController(bhkCharRigidBodyControllerCinfo& cinfo);
        virtual ~bhkCharRigidBodyController();

        virtual void          GetPositionImpl(hkVector4& pos, bool applyCenterOffset) const;
        virtual void          SetPositionImpl(const hkVector4& pos, bool applyCenterOffset, bool moveByVelocity);
        virtual void          GetTransformImpl(hkTransform& transform) const;
        virtual void          SetTransformImpl(const hkTransform& transform);
        virtual void          GetLinearVelocityImpl(hkVector4& velocity) const;
        virtual void          SetLinearVelocityImpl(const hkVector4& velocity);
        virtual CFilter       GetCollisionFilterInfo() const;
        virtual void          SetCollisionFilterInfo(CFilter filter);
        virtual bool          Unk_0A();
        virtual bool          Unk_0B();
        virtual void          Unk_0C();
        virtual void          CheckSupportImpl();
        virtual void          SetWorld(bhkWorld* world);
        virtual bhkWorld*     GetHavokWorld() const;
        virtual hkpRigidBody* GetRigidBody() const;
        virtual float         GetVDBAlpha() const;
        virtual bool          GetContactCollidables(BSScrapArray<hkpCollidable*>& collidables);
        virtual void          RotateImpl(const hkTransform& transform);

        /// @brief After each step: tells the move-finish sinks where the character is, clears kCanJump, runs
        ///        Havok's callback and recounts the character contacts.
        virtual void characterCallback(hkpWorld* world, hkpCharacterRigidBody* characterRB);
        /// @brief Havok's slope planes, except on contacts whose material lets the character walk up (IsSlopeBlocking
        ///        false), and with the plane taken against the sign-corrected dot.
        virtual void considerCollisionEntryForSlope(const hkpWorld* world, hkpCharacterRigidBody* characterRB,
                                                    const hkpLinkedCollidable::CollisionEntry& entry, hkpSimpleConstraintContactMgr* mgr,
                                                    hkArray<hkContactPointId>& contactPointIds);

        /// @brief Builds the Havok side from `cinfo`: completes its rigid-body cinfo (shape, filter, up, speed
        ///        limit), builds the bhkCharacterRigidBody and its bhkRigidBody, listens to it and enters the cinfo's world.
        /// @note Name provisional.
        void Init(bhkCharRigidBodyControllerCinfo& cinfo);

        /// @brief Counts the character-controller bodies (layer 30) in contact, of the controller's own collision
        ///        group if it has one.
        /// @note Name provisional. noinline: vanilla calls it from Unk_0B and characterCallback, where /Ob2 would
        ///       otherwise inline it.
        __declspec(noinline) void ProcessCharacterContacts();

        /// @brief (1, 1, 0, 0): dotted with a velocity, its x + y.
        static const hkVector4 s_horizontalSum;

        bhkCharacterRigidBody       m_characterRigidBody;                ///< +0x340
        /// @brief The Bethesda wrapper of the controller's hkpRigidBody (adds it to / removes it from the world).
        NiPointer<bhkRigidBody>     m_rigidBody;                         ///< +0x360
        hkUint8                     m_pad368[0x370 - 0x368];
        bhkCharacterPointCollector  m_ignoredCollisionStartCollector;    ///< +0x370
    };

    /// @brief `shape`, seen through a MOPP, if it is one of the mesh-like shape types (16, 24, 31, 32); null otherwise.
    /// @note Name provisional; out of line in vanilla (0x140e8c180).
    const hkpShape* bhkGetMeshShape(const hkpShape* shape);

    /// @brief Sets the visual-debugger color of `body`.
    /// @note Name provisional; an empty function in the shipped exe (0x140ebc870).
    void bhkSetDebugDisplayColor(hkpRigidBody* body, hkColor::Argb color);
} }
