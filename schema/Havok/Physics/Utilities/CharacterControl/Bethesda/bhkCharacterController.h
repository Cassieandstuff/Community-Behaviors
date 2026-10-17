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
    class hkpWorldObject;
    class hkpCapsuleShape;
    class bhkCharacterController;
    struct hkContactPoint;
    struct hkRootCdPoint;
    struct hkpCharacterObjectInteractionEvent;
    struct hkpCharacterObjectInteractionResult;
    class bhkCollisionBox;

    /// @brief What the actor asks of its character controller for one frame (bhkCharacterController::UpdatePhysics).
    /// @note Name provisional; 0x24 bytes as far as UpdatePhysics reads it. UpdatePhysics writes some of it back.
    struct bhkCharacterMoveData
    {
        /// @brief The frame's time, in seconds (UpdatePhysics divides it among the sub-steps).
        float    m_deltaTime;   ///< +0x00
        float    m_pitch;       ///< +0x04
        float    m_roll;        ///< +0x08
        /// @brief The character's heading (radians about the up axis); UpdatePhysics stores it back turned by m_rotMod.
        float    m_heading;     ///< +0x0c
        /// @brief The movement asked for this frame, in game units, in the character's frame (y forward, x to its
        ///        side, z up); UpdatePhysics divides it among the sub-steps.
        NiPoint3 m_movement;    ///< +0x10
        /// @brief The movement speed in percent (UpdatePhysics stores it as a fraction in m_speedPct).
        float    m_speedPct;    ///< +0x1c
        /// @brief Forces the support query each step, even standing still on the ground.
        /// @note Name provisional.
        bool     m_forceSupportCheck;   ///< +0x20
    };

    /// @brief (1, 1, 0, 0): its dot product with a velocity sums the horizontal components.
    /// @note Name provisional; a global set by a static initializer.
    extern hkVector4 g_characterHorizontal;

    /// @brief What turns a character toward where it is going.
    /// @note Only the slots the controller calls are named.
    class bhkICharOrientationController
    {
    public:
        virtual ~bhkICharOrientationController();   ///< slot 0
        virtual void Unk_01();
        /// @brief UpdatePhysics calls it on `controller` before each support check.
        virtual void Unk_02(bhkCharacterController* controller);   ///< slot 2
        /// @brief Told of a contact the character stands on: one the proxy controller's constraints callback takes as
        ///        support (outside the support check), or one TryStep lets it step up onto.
        /// @note Name provisional; empty in the biped controller.
        virtual void OnSupportContact(const hkRootCdPoint& point);   ///< slot 3
        /// @brief Where the step test probes from: may move `position` (the character's position on entry) and returns
        ///        which of the controller's points it took, or -1 to have TryStep raise `position` by the step height.
        /// @param capsule The character's capsule, null when its shape is not one.
        /// @note Name provisional; the biped controller returns -1 (its position untouched).
        virtual int GetStepProbe(bhkCharacterController* ctrl, const hkpCapsuleShape* capsule, const hkVector4& contactPosition,
                                 hkVector4& position);   ///< slot 4
        virtual void Unk_05();
        /// @brief Told that `ctrl` pushed `other` at `contact` (PushCharacter).
        /// @note Name provisional; empty in the biped controller.
        virtual void OnPushCharacter(bhkCharacterController* ctrl, const hkContactPoint& contact, bhkCharacterController* other);   ///< slot 6
        /// @brief Whether a contact `maxHeight` or more above the probe (at height `contactZ`, against probe point
        ///        `probe`) is too high to step onto; may set `stepUp`.
        /// @note Name provisional; the biped controller always rejects; the quadruped one measures from its probe
        ///       point, rejecting above `maxHeight` and setting `stepUp` below it.
        virtual bool RejectStep(bhkCharacterController* ctrl, float maxHeight, float contactZ, int probe, bool& stepUp);   ///< slot 7
        /// @brief UpdatePhysics calls it on `controller` while m_calculatePitchTimer runs (see m_updatePitch).
        virtual void Unk_08(bhkCharacterController* controller);   ///< slot 8
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
        /// @brief The step-up test a contact callback runs on a contact too steep to stand on: whether the character
        ///        may step up onto `point`'s body B (the contacted body; A is the character's).
        /// @note Name provisional; out of line in vanilla (0x140ea22e0).
        bool TryStep(const hkRootCdPoint& point);
        /// @brief Reads the surface under `point` (a contact with body B) into m_contactLayer, m_contactVelocity and
        ///        m_surfaceMaterial, and sets kOnStairs (flag bit 24) by the material; a no-op once m_surfaceMaterial is
        ///        set.
        /// @note Name provisional; out of line in vanilla (0x140ea21a0).
        void UpdateContactMaterial(const hkRootCdPoint& point);
        /// @brief Records `other`'s body as m_bumpedCharCollisionObject, then, unless `other` outranks this one
        ///        (m_pushPriority) or is kNotPushable (flag bit 14), pushes it against `contact`'s horizontal normal by a
        ///        quarter of this character's horizontal speed: the push goes into its m_pushDelta (and m_pushCount), and
        ///        100 times it becomes its m_initialVelocity (m_velocityTime 0.01) when faster than the one it has.
        /// @note Name provisional; out of line in vanilla (0x140ea13a0).
        void PushCharacter(bhkCharacterController* other, const hkContactPoint& contact);
        /// @brief The controller `object` belongs to (its Havok property 1002), null if none.
        /// @note Name provisional; out of line in vanilla (0x140ea0e20).
        static bhkCharacterController* GetControllerFromCollidable(const hkpWorldObject* object);
        /// @brief The value of `object`'s Havok property `key` (read under the world lock), 0 if it has none.
        /// @note Name provisional; out of line in vanilla (0x140ea0cf0).
        static hkUlong GetHavokProperty(hkUint32 key, const hkpWorldObject* object);
        /// @brief Makes `body` (referenced; null for none) the support body.
        /// @note Name provisional; out of line in vanilla (0x140ea0560).
        void SetSupportBody(hkpRigidBody* body);
        /// @brief Scales the impulse the proxy would give the body it pushes (by the body's collision layer, its mass
        ///        and the impulse), lets the orientation controller see it under kNoFriction, and keeps the body as
        ///        m_bumpedBody when it is the hardest push yet.
        /// @note Name provisional; out of line in vanilla (0x140ea1600).
        void HandleObjectInteraction(const hkpCharacterObjectInteractionEvent& input, hkpCharacterObjectInteractionResult& output);
        /// @note Out of line in vanilla (0x140ea0dc0); `body` may be null.
        static bool IsHurtfulBody(hkpRigidBody* body);
        /// @brief Records a contact at `point` with a hurtful body.
        /// @note Out of line in vanilla (0x140ea10b0).
        void ProcessHurtfulBody(hkpRigidBody* body, const hkContactPoint* point);
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
        /// @brief Enters m_wantState, if one is requested, and clears the request.
        /// @note Name provisional; out of line in vanilla (0x140ea0350). It stores the context's current state
        ///       directly: no leaveState / enterState runs. Every state's Simulate but Flying's commits through it.
        void CommitWantedState();
        /// @brief Adds this step's time to m_fallTime.
        /// @note Name provisional; out of line in vanilla (0x140ea0380).
        void AccumulateFallTime();
        /// @brief How far (game units) the character is below m_fallStartHeight, 0 if above it or with no fall start.
        /// @note Name provisional; out of line in vanilla (0x140e9eec0).
        float GetFallDistance();
        /// @brief Adds m_initialVelocity scaled by m_velocityTime to the output velocity while m_velocityTime is positive
        ///        (not when chasing or following a ragdoll); zeroes m_initialVelocity once it is not.
        /// @note Name provisional; out of line in vanilla (0x140ea0470).
        void ApplyInitialVelocity();
        /// @brief `heading` turned by m_rotMod over this step's share of m_rotModTime (which it spends, while m_rotMod
        ///        drops by 0.05); m_rotMod zeroed and `heading` unchanged once m_rotModTime is spent.
        /// @note Name provisional; out of line in vanilla (0x140ea0500).
        float UpdateHeading(float heading, float deltaTime);
        /// @brief One frame of the character's physics: flying moves the body straight to `data`'s movement; any
        ///        other state runs one or two sub-steps of velocity, support query and state Simulate (a controller
        ///        flagged kNoSim only moves its position).
        /// @return 0, 3 when the last sub-step's Unk_0B failed, 2 when kHitDamage is set.
        /// @note Name per the community; out of line in vanilla (0x140e9f170). Actor's per-frame move calls it.
        hkUint32 UpdatePhysics(bhkCharacterMoveData& data);
        /// @note Name provisional; out of line in vanilla (0x140ea0170), not reimplemented.
        void UpdateSlopeTilt(float deltaTime);
        /// @brief Turns the controller's body to `orientation` (with the pitch and roll it is flagged for).
        /// @note Name provisional; out of line in vanilla (0x140ea06e0), not reimplemented.
        void SetOrientation(const hkQuaternion& orientation);

        /// @brief UpdatePhysics stores bhkCharacterMoveData::m_speedPct divided by it (0.01) in m_speedPct.
        /// @note Name provisional; a variable in vanilla's .data.
        static float s_speedPctDivisor;
        /// @brief A movement shorter than its square root (10, squared game units) never takes a second sub-step.
        /// @note Name provisional; a variable in vanilla's .data.
        static float s_singleStepLengthSq;
        /// @brief What m_calculatePitchTimer restarts at (5 seconds).
        /// @note Name provisional; a variable in vanilla's .data.
        static float s_calculatePitchTime;

        hkUint8        m_pad68[0x70 - 0x68];
        /// @brief The character's forward direction (the states steer in its frame).
        hkVector4      m_forward;           ///< +0x70
        hkStepInfo     m_stepInfo;          ///< +0x80
        /// @brief The velocity the controller drives the body toward.
        hkVector4      m_outVelocity;       ///< +0x90
        /// @brief A velocity added each grounded or swimming step while m_velocityTime is positive (scaled by it), zeroed
        ///        once it is not.
        /// @note Name per CommonLibSSE.
        hkVector4      m_initialVelocity;   ///< +0xa0
        /// @brief The movement the character asks for, in its own frame: y along m_forward, x to its side, z up.
        /// @note Name per CommonLibSSE.
        hkVector4      m_velocityMod;       ///< +0xb0
        /// @brief Where the body sits from the character's position, in the body's frame (GetPositionImpl and
        ///        SetPositionImpl rotate it by the body's rotation).
        hkVector4      m_centerOffset;      ///< +0xc0
        /// @brief How far other characters pushed this one (each PushCharacter adds its push), in Havok units; zeroed
        ///        (with m_pushCount) once the character asks to move.
        /// @note Name per CommonLibSSE (which places it at +0xe0).
        hkVector4      m_pushDelta;         ///< +0xd0
        /// @brief Where InAir's stuck recovery put the character on the ground (see m_fakeSupport).
        /// @note Name per CommonLibSSE (which places it at +0xf0).
        hkVector4      m_fakeSupportStart;  ///< +0xe0
        /// @brief The character's up direction (the rigid-body controller builds its Havok body with it).
        hkVector4      m_up;                ///< +0xf0
        /// @brief The linear velocity of the rigid body behind the contact UpdateContactMaterial read, zero for none.
        /// @note Name provisional.
        hkVector4      m_contactVelocity;   ///< +0x100
        /// @brief The support query's surface normal (CheckSupportImpl copies it from m_surfaceInfo).
        hkVector4      m_supportNorm;       ///< +0x110
        BSBound        m_collisionBound;         ///< +0x120
        BSBound        m_bumperCollisionBound;   ///< +0x150
        /// @brief The body's velocity in game units per second, as of the last step.
        /// @note Name provisional.
        NiPoint3       m_gameVelocity;      ///< +0x180
        hkUint8        m_pad18c[0x190 - 0x18c];
        bhkICharOrientationController* m_orientationCtrl;   ///< +0x190
        hkUint8        m_pad198[0x1a0 - 0x198];
        hkpSurfaceInfo m_surfaceInfo;       ///< +0x1a0
        hkpCharacterContext m_context;      ///< +0x1e0
        /// @brief CHARACTER_FLAGS: bit 2 kTryStep, 3 kNoFriction, 4 kAllowJumpNoContact, 5 kStuckQuad, 7 kHitFlags,
        ///        8 kSupport, 9 kHasPotentialSupportManifold, 10 kCanJump, 11 kChaseBip, 12 kFollowRagdoll, 13 kJumping,
        ///        16 kCheckSupport, 17 kNoSim (not simulated), 19 kOnStilts, 21 kRecordHits, 23 kShouldersUnderWater,
        ///        27 kNoCharacterCollisions, 29 kPossiblePathObstacle (names per CommonLibSSE).
        hkUint32       m_flags;             ///< +0x218
        /// @brief The state the next CommitWantedState enters; HK_CHARACTER_MAX_STATE_ID (0xb) for no request.
        hkpCharacterStateType m_wantState;   ///< +0x21c
        /// @brief How much of m_initialVelocity to add this step (see ApplyInitialVelocity).
        /// @note Name per CommonLibSSE.
        float          m_velocityTime;      ///< +0x220
        /// @brief The heading change UpdateHeading adds per second while m_rotModTime is positive.
        /// @note Name per CommonLibSSE.
        float          m_rotMod;            ///< +0x224
        /// @brief How long m_rotMod still turns the character, in seconds.
        /// @note Name per CommonLibSSE.
        float          m_rotModTime;        ///< +0x228
        /// @brief Seconds the orientation controller still updates the pitch for (reset to 5 when the character moves
        ///        or turns).
        /// @note Name per CommonLibSSE.
        float          m_calculatePitchTimer;   ///< +0x22c
        /// @brief The air-control skill: InAir steers only while it is positive.
        /// @note Name per CommonLibSSE.
        float          m_acrobatics;        ///< +0x230
        /// @brief The center offset's height (UpdatePhysics copies it into m_centerOffset's z).
        /// @note Name per CommonLibSSE.
        float          m_center;            ///< +0x234
        /// @brief The water surface's height, in Havok units.
        /// @note Name per CommonLibSSE.
        float          m_waterHeight;       ///< +0x238
        /// @brief The height a jump reaches (its sign ignored), in Havok units.
        float          m_jumpHeight;        ///< +0x23c
        /// @brief The highest point of the current fall, in game units.
        float          m_fallStartHeight;   ///< +0x240
        /// @brief Seconds in the air (InAir adds each step's time and zeroes it as it leaves).
        float          m_fallTime;          ///< +0x244
        /// @brief The character's gravity multiplier.
        float          m_gravity;           ///< +0x248
        /// @note Names per CommonLibSSE; the flying update stores the frame's pitch and roll in them.
        float          m_pitchAngle;        ///< +0x24c
        float          m_rollAngle;         ///< +0x250
        /// @note Name per CommonLibSSE; UpdatePhysics caps it at 1 before the pitch update.
        float          m_pitchMult;         ///< +0x254
        hkUint8        m_pad258[0x25c - 0x258];
        /// @brief Swimming treats the character as submerged from 0.5 up (and floats it higher by it).
        /// @note Name per CommonLibSSE.
        float          m_swimFloatHeight;   ///< +0x25c
        /// @note Name per CommonLibSSE; Swimming scales the float depth by it.
        float          m_actorHeight;       ///< +0x260
        /// @note Name per CommonLibSSE; UpdatePhysics stores bhkCharacterMoveData::m_speedPct / 0.01 in it.
        float          m_speedPct;          ///< +0x264
        /// @brief How many times other characters pushed this one (nonzero while m_pushDelta holds a push).
        /// @note Name per CommonLibSSE.
        hkUint32       m_pushCount;         ///< +0x268
        hkUint8        m_pad26c[0x270 - 0x26c];
        /// @brief A counted Bethesda physics object (InitFromCinfo moves it into the cinfo's world).
        NiPointer<bhkSerializable> m_unk270;   ///< +0x270
        /// @brief Which of m_shapes is in use (2 or more: none).
        hkInt32        m_shapeIndex;        ///< +0x278
        hkUint8        m_pad27c[0x280 - 0x27c];
        NiPointer<bhkShape> m_shapes[2];    ///< +0x280
        /// @brief The character's radius, in Havok units (TryStep probes with the body's own radius when it is a
        ///        capsule).
        /// @note Name provisional.
        float          m_radius;            ///< +0x290
        hkUint8        m_pad294[0x2a0 - 0x294];
        /// @brief While it is 2, PushCharacter adds 20 to the speed it pushes with (before scaling it by -0.25).
        /// @note Meaning unknown.
        hkUint32       m_unk2a0;            ///< +0x2a0
        /// @brief A character pushes only characters whose priority is at most its own.
        /// @note Name provisional.
        hkUint32       m_pushPriority;      ///< +0x2a4
        /// @brief How many manifold points the proxy controller's last constraints callback counted as support.
        /// @note Name per CommonLibSSE.
        hkInt32        m_supportCount;      ///< +0x2a8
        hkUint8        m_pad2ac[0x2b0 - 0x2ac];
        hkRefPtr<hkpRigidBody> m_supportBody;                 ///< +0x2b0
        /// @note Name per CommonLibSSE; UpdatePhysics zeroes it with m_bumpedBody.
        float          m_bumpedForce;       ///< +0x2b8
        hkUint8        m_pad2bc[0x2c0 - 0x2bc];
        hkRefPtr<hkpRigidBody> m_bumpedBody;                  ///< +0x2c0
        hkRefPtr<hkpRigidBody> m_bumpedCharCollisionObject;   ///< +0x2c8
        /// @brief Per CommonLibSSE, damage impacts by body (the value a DamageImpactData*).
        BSTHashMap<NiPointer<bhkRigidBody>, void*> m_damageImpacts;   ///< +0x2d0
        /// @brief OnGround's unsupported cast sets kAllowJumpNoContact (flag bit 4) when its hit's normal z is below it.
        /// @note Name per CommonLibSSE.
        float          m_zNormal;           ///< +0x300
        /// @brief The material under the contact UpdateContactMaterial read (0 until one does; UpdatePhysics zeroes it
        ///        and m_contactLayer each simulated frame of a controller without a rigid body).
        /// @note Name provisional.
        hkUint32       m_surfaceMaterial;     ///< +0x304
        /// @brief That contact's collision layer.
        /// @note Name provisional.
        hkUint32       m_contactLayer;        ///< +0x308
        /// @brief How high the character steps up, in Havok units.
        /// @note Name provisional.
        float          m_stepHeight;          ///< +0x30c
        /// @brief How far past its radius TryStep probes for the step's face, in Havok units.
        /// @note Name provisional.
        float          m_stepProbeDistance;   ///< +0x310
        /// @brief How many character-controller bodies (layer 30) of m_systemGroup the controller touches.
        hkUint32       m_numCharacterContacts;   ///< +0x314
        /// @brief The collision system group the controller counts contacts of, 0 for any.
        hkUint32       m_systemGroup;            ///< +0x318
        /// @brief Set by the proxy controller's constraints callback when every point it counted as support was one it
        ///        also gave a vertical plane (a step TryStep refused); a supported surface then gives no kSupport in
        ///        UpdatePhysics (the support-manifold flag, bit 9, still does).
        /// @note Name provisional (CommonLibSSE: unk31C).
        bool           m_onlyWallContacts;  ///< +0x31c
        /// @brief When set, each counted character contact stops the controller (zero velocity).
        bool           m_stopOnCharacterContact;   ///< +0x31d
        /// @brief Set by InAir's stuck recovery: OnGround then keeps the character grounded until it is 0.9144 Havok
        ///        units from m_fakeSupportStart or a state is requested.
        /// @note Name provisional.
        bool           m_fakeSupport;       ///< +0x31e
        /// @brief Lets the orientation controller update the pitch while m_calculatePitchTimer runs.
        /// @note Name provisional.
        bool           m_updatePitch;       ///< +0x31f
        hkUint8        m_pad320[0x330 - 0x320];
    };

    /// @brief Whether `type` moves a body by itself: dynamic, or sphere, box or thin-box inertia (not keyframed, fixed
    ///        or character).
    /// @note Name provisional; out of line in vanilla (0x140e94a00), which takes the type by reference.
    bool bhkIsDynamicMotionType(const hkpMotion::MotionType& type);
} }
