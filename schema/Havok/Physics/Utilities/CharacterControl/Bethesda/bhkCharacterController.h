/// @file
/// @brief bhkCharacterController: Bethesda's character controller — the state machine and movement state an actor's
///        physics runs on, over either Havok controller (bhkCharRigidBodyController, bhkCharProxyController).
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/NiRefObject.h>
#include <Physics/Utilities/CharacterControl/Bethesda/MemoryManager.h>
#include <Physics/Utilities/CharacterControl/Bethesda/BSTEventSource.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkShape.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkRigidBody.h>
#include <Physics/Utilities/CharacterControl/Bethesda/NiExtraData.h>
#include <Physics/Utilities/CharacterControl/Bethesda/BSTHashMap.h>
#include <Physics/Utilities/CharacterControl/Bethesda/BSTArray.h>
#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterContext.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Utilities/CharacterControl/hkpCharacterControl.h>
#include <Common/Base/Types/Physics/hkStepInfo.h>

namespace CB { namespace schema
{
    class bhkWorld;
    class hkpRigidBody;
    class hkpCollidable;

    /// @brief What turns a character toward where it is going.
    /// @note Only the slots the controller calls are named.
    class bhkICharOrientationController
    {
    public:
        virtual ~bhkICharOrientationController();   ///< slot 0
        virtual void Unk_01();
        virtual void Unk_02();
        virtual void Unk_03();
        virtual void Unk_04();
        virtual void Unk_05();
        virtual void Unk_06();
        virtual void Unk_07();
        virtual void Unk_08();
        /// @brief Resets the controller (ResetContactState calls it).
        virtual void Reset();                         ///< slot 9 — name provisional
        /// @brief Whether the character controller owns (and deletes) it.
        virtual bool IsOwnedByController();           ///< slot 10 — name provisional
    };

    /// @brief A Havok collision filter info: layer in the low 7 bits, then the group and system bits.
    /// @note 4 bytes.
    struct CFilter
    {
        hkUint32 m_filter;
    };

    /// @brief What a character controller sends its sinks once the body has moved.
    /// @note 0xc bytes.
    class bhkCharacterMoveFinishEvent
    {
    public:
        /// @brief The character's position (the body's less its center offset), in game units.
        NiPoint3 m_position;   ///< +0x00
    };

    /// @note 0x330 bytes; vtable 0x1419e4338 (every slot from 2 on pure).
    class bhkCharacterController : public NiRefObject, public BSTEventSource<bhkCharacterMoveFinishEvent>
    {
    public:
        CB_BETHESDA_HEAP_ALIGNED_NEW_DELETE

        /// @note Out of line in vanilla (0x140e9dcd0).
        explicit bhkCharacterController(const bhkCharacterControllerCinfo& cinfo);
        /// @note Out of line in vanilla (0x140e9e060).
        virtual ~bhkCharacterController();

        /// @brief The body's position, less the rotated center offset when `applyCenterOffset`.
        virtual void          GetPositionImpl(hkVector4& pos, bool applyCenterOffset) const = 0;               ///< slot 2
        virtual void          SetPositionImpl(const hkVector4& pos, bool applyCenterOffset, bool moveByVelocity) = 0;   ///< slot 3
        virtual void          GetTransformImpl(hkTransform& transform) const = 0;                               ///< slot 4
        virtual void          SetTransformImpl(const hkTransform& transform) = 0;                               ///< slot 5
        virtual void          GetLinearVelocityImpl(hkVector4& velocity) const = 0;                             ///< slot 6
        virtual void          SetLinearVelocityImpl(const hkVector4& velocity) = 0;                             ///< slot 7
        virtual CFilter       GetCollisionFilterInfo() const = 0;                                               ///< slot 8
        virtual void          SetCollisionFilterInfo(CFilter filter) = 0;                                       ///< slot 9
        virtual bool          Unk_0A() = 0;                                                                     ///< slot 10
        virtual bool          Unk_0B() = 0;                                                                     ///< slot 11
        virtual void          Unk_0C() = 0;                                                                     ///< slot 12
        /// @brief Refreshes the surface info (+0x1a0) and the support normal from the controller's support query.
        virtual void          CheckSupportImpl() = 0;                                                           ///< slot 13
        virtual void          SetWorld(bhkWorld* world) = 0;                                                    ///< slot 14
        virtual bhkWorld*     GetHavokWorld() const = 0;                                                        ///< slot 15
        virtual hkpRigidBody* GetRigidBody() const = 0;                                                         ///< slot 16
        virtual float         GetVDBAlpha() const = 0;                                                          ///< slot 17
        /// @brief Appends the collidables the controller is in contact with.
        /// @note Name provisional.
        virtual bool          GetContactCollidables(BSScrapArray<hkpCollidable*>& collidables) = 0;                     ///< slot 18
        virtual void          RotateImpl(const hkTransform& transform) = 0;                                         ///< slot 19

        /// @brief Whether the controller is in a bhkWorld.
        /// @note Name provisional. Non-virtual on purpose: called from a destructor it still dispatches through the
        ///       vtable, where a direct call would be bound statically (and inlined).
        HK_FORCE_INLINE bool IsInWorld() const { return GetHavokWorld() != HK_NULL; }
        /// @brief Whether CHARACTER_FLAGS bit `bit` is set.
        HK_FORCE_INLINE bool IsFlagSet(int bit) const { return (m_flags >> bit) & 1; }

        /// @brief Clears kTryStep and the support normal, and resets the orientation controller.
        /// @note Name provisional; out of line in vanilla (0x140ea0320).
        void ResetContactState();
        /// @brief The base half of building a controller from `cinfo` (flags, state, shapes, bounds).
        /// @note Name provisional; out of line in vanilla (0x140ea17e0).
        void InitFromCinfo(const bhkCharacterControllerCinfo& cinfo);
        /// @brief Gives the controller's Havok body the property `key` = `value`, unless it has one already.
        /// @note Name provisional; out of line in vanilla (0x140e9ef80).
        void AddHavokProperty(hkUint32 key, hkUlong value);
        /// @brief Empties m_damageImpacts (keeping its entry block).
        /// @note Name provisional; out of line in vanilla (0x140ea0c10).
        void ClearDamageImpacts();
        /// @brief Whether a body on collision layer `layer` counts for Unk_0A: false for clutter, weapon, projectile,
        ///        spell, props, water, trigger, cloud trap, ground, actor zone, projectile zone and gas trap (layers 4-7,
        ///        10-12, 16, 17, 22-24), true for every other.
        /// @note Name provisional; out of line in vanilla (0x140ea2c00).
        static bool IsLayerCounted(hkUint32 layer);
        /// @brief Whether the contact with `shape` (a mesh-like shape, as GetMeshShape returns) at `shapeKey` may get a
        ///        vertical plane: the game's material predicate on the shape key's material (on the wrapper's own
        ///        material for an invalid key), negated; true without a material lookup when the flags say so.
        /// @note Name and the two flags' meaning provisional (vanilla passes true, true); out of line in vanilla
        ///       (0x140ea2b90).
        static bool IsSlopeBlocking(const hkpShape* shape, bool checkMaterial, bool defaultResult, hkUint32 shapeKey);

        hkUint8        m_pad68[0x80 - 0x68];
        hkStepInfo     m_stepInfo;          ///< +0x80
        /// @brief The velocity the controller drives the body toward.
        hkVector4      m_outVelocity;       ///< +0x90
        hkUint8        m_pada0[0xc0 - 0xa0];
        /// @brief Where the body sits from the character's position, in the body's frame (GetPositionImpl and
        ///        SetPositionImpl rotate it by the body's rotation).
        hkVector4      m_centerOffset;      ///< +0xc0
        hkUint8        m_padd0[0xf0 - 0xd0];
        /// @brief The character's up direction (the rigid-body controller builds its Havok body with it).
        hkVector4      m_up;                ///< +0xf0
        hkUint8        m_pad100[0x110 - 0x100];
        /// @brief The support query's surface normal (CheckSupportImpl copies it from m_surfaceInfo).
        hkVector4      m_supportNorm;       ///< +0x110
        BSBound        m_collisionBound;         ///< +0x120
        BSBound        m_bumperCollisionBound;   ///< +0x150
        hkUint8        m_pad180[0x190 - 0x180];
        bhkICharOrientationController* m_orientationCtrl;   ///< +0x190
        hkUint8        m_pad198[0x1a0 - 0x198];
        hkpSurfaceInfo m_surfaceInfo;       ///< +0x1a0
        hkpCharacterContext m_context;      ///< +0x1e0
        /// @brief CHARACTER_FLAGS: bit 3 kNoFriction, 8 kSupport, 10 kCanJump, 17 kNoSim (not simulated).
        hkUint32       m_flags;             ///< +0x218
        hkUint8        m_pad21c[0x270 - 0x21c];
        /// @brief A counted Bethesda physics object (InitFromCinfo moves it into the cinfo's world).
        NiPointer<bhkSerializable> m_unk270;   ///< +0x270
        /// @brief Which of m_shapes is in use (2 or more: none).
        hkInt32        m_shapeIndex;        ///< +0x278
        hkUint8        m_pad27c[0x280 - 0x27c];
        NiPointer<bhkShape> m_shapes[2];    ///< +0x280
        hkUint8        m_pad290[0x2b0 - 0x290];
        hkRefPtr<hkpRigidBody> m_supportBody;                 ///< +0x2b0
        hkUint8        m_pad2b8[0x2c0 - 0x2b8];
        hkRefPtr<hkpRigidBody> m_bumpedBody;                  ///< +0x2c0
        hkRefPtr<hkpRigidBody> m_bumpedCharCollisionObject;   ///< +0x2c8
        /// @brief Per CommonLibSSE, damage impacts by body (the value a DamageImpactData*).
        BSTHashMap<NiPointer<bhkRigidBody>, void*> m_damageImpacts;   ///< +0x2d0
        hkUint8        m_pad300[0x314 - 0x300];
        /// @brief How many character-controller bodies (layer 30) of m_systemGroup the controller touches.
        hkUint32       m_numCharacterContacts;   ///< +0x314
        /// @brief The collision system group the controller counts contacts of, 0 for any.
        hkUint32       m_systemGroup;            ///< +0x318
        hkUint8        m_pad31c[0x31d - 0x31c];
        /// @brief When set, each counted character contact stops the controller (zero velocity).
        bool           m_stopOnCharacterContact;   ///< +0x31d
        hkUint8        m_pad31e[0x330 - 0x31e];
    };
} }
