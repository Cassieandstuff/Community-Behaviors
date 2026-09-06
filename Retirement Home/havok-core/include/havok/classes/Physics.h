#pragma once
#include "havok/classes/Base.h"       // hkReferencedObject
#include "havok/classes/Animation.h"  // hkaSkeleton (ragdoll instance + skeleton mapper)
#include "havok/core/HkTypes.h"       // Vector4, Half, QSTransform

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Havok physics classes — the ragdoll rig a skeleton.hkx carries alongside its skeletons. Ported
// byte-exact from serde-hkx layouts (the SOP reference). The collision-shape hierarchy first:
// hkpCapsuleShape is the capsule around each ragdoll bone. Abstract bases (hkpShape/SphereRep/Convex)
// are pointer targets + field carriers; only the concrete leaf (hkpCapsuleShape) is instantiated.

namespace havok {

// hkpShape — size 32, sig 0x666490a1. Physics collision-shape base. m_userData (hkUlong) @16; the
// m_type enum (SERIALIZE_IGNORED — derivable from the class) + tail pad fill 24..31.
class hkpShape : public hkReferencedObject {
public:
    std::uint64_t m_userData = 0;
    HK_CLASS_ID(0x666490a1u, "hkpShape")
};

// hkpSphereRepShape — size 32, sig 0xe7eca7eb. Abstract intermediate, no own serialized members.
class hkpSphereRepShape : public hkpShape {
public:
    HK_CLASS_ID(0xe7eca7ebu, "hkpSphereRepShape")
};

// hkpConvexShape — size 40, sig 0xf8f74f85. m_radius (hkReal) @32 + pad to 40.
class hkpConvexShape : public hkpSphereRepShape {
public:
    float m_radius = 0.f;
    HK_CLASS_ID(0xf8f74f85u, "hkpConvexShape")
};

// hkpCapsuleShape — size 80, sig 0xdd0b1fd3. The two capsule end vertices @48/@64 (pad 40->48 to
// 16-align the first Vector4).
class hkpCapsuleShape : public hkpConvexShape {
public:
    Vector4 m_vertexA{};
    Vector4 m_vertexB{};
    HK_CLASS_ID(0xdd0b1fd3u, "hkpCapsuleShape")
};

// ── The rigid-body subtree (a skeleton.hkx's ragdoll rig) ───────────────────────
// Ported from serde-hkx layouts. hkTransform / hkpPropertyValue are plain value types (no Havok
// class signature) with only Read/Write — usable as hkArray elements and by-value members. The
// inline Havok structs (hkMotionState, hkpMaterial, …) carry HK_CLASS_ID but are NOT registered;
// only the top-level objects hkpRigidBody + hkpShapeInfo are REG'd.

// hkTransform — 64-byte value type: 4 consecutive Vector4 (rotation columns + translation).
struct hkTransform {
    std::array<Vector4, 4> m_data{};
    void Read(PackFileDeserializer& des, BinaryReaderEx& br);
    void Write(PackFileSerializer& s, BinaryWriterEx& bw) const;
};

// hkpPropertyValue — 8-byte union (hkUint64 / void* / hkReal), stored raw.
struct hkpPropertyValue {
    std::uint64_t m_data = 0;
    void Read(PackFileDeserializer& des, BinaryReaderEx& br);
    void Write(PackFileSerializer& s, BinaryWriterEx& bw) const;
};

// hkMotionState — 176B, sig 0x5797386e. Inline value struct: transform + swept transform + a scalar
// tail. Contains Vector4s, so 173 rounds up to 176 (3-byte tail pad).
class hkMotionState : public IHavokObject {
public:
    std::array<Vector4, 4> m_transform{};        // hkTransform      @0   (64B)
    std::array<Vector4, 5> m_sweptTransform{};   // hkSweptTransform @64  (80B)
    Vector4                m_deltaAngle{};        // @144
    float                  m_objectRadius = 0.f;  // @160
    Half                   m_linearDamping = 0;   // @164 hkHalf raw
    Half                   m_angularDamping = 0;  // @166 hkHalf raw
    Half                   m_timeFactor = 0;      // @168 hkHalf raw
    std::uint8_t           m_maxLinearVelocity = 0;   // @170
    std::uint8_t           m_maxAngularVelocity = 0;  // @171
    std::uint8_t           m_deactivationClass = 0;   // @172
    HK_CLASS_ID(0x5797386eu, "hkMotionState")
};

// hkpMaterial — 16B (12 natural, rounds to 16), sig 0x33be6570. Inline value struct.
class hkpMaterial : public IHavokObject {
public:
    std::uint8_t m_responseType = 0;               // @0 enum ResponseType (hkInt8)
    Half         m_rollingFrictionMultiplier = 0;  // @2 hkHalf raw
    float        m_friction = 0.f;                 // @4
    float        m_restitution = 0.f;              // @8
    HK_CLASS_ID(0x33be6570u, "hkpMaterial")
};

// hkpEntitySpuCollisionCallback — 16B, sig 0x81147f05. Inline value struct.
class hkpEntitySpuCollisionCallback : public IHavokObject {
public:
    std::uint8_t m_eventFilter = 0;   // @10
    std::uint8_t m_userFilter  = 0;   // @11
    HK_CLASS_ID(0x81147f05u, "hkpEntitySpuCollisionCallback")
};

// hkpProperty — 16B, sig 0x9ce308e9. hkArray element on hkpWorldObject.
class hkpProperty : public IHavokObject {
public:
    std::uint32_t    m_key = 0;
    std::uint32_t    m_alignmentPadding = 0;
    hkpPropertyValue m_value{};
    HK_CLASS_ID(0x9ce308e9u, "hkpProperty")
};

// hkMultiThreadCheck — 12B, sig 0x11e4408b. All fields SERIALIZE_IGNORED (a 12-byte skip).
class hkMultiThreadCheck : public IHavokObject {
public:
    HK_CLASS_ID(0x11e4408bu, "hkMultiThreadCheck")
};

// hkpTypedBroadPhaseHandle — 12B, sig 0xf4b0f799. Inlines its hkpBroadPhaseHandle base (a 4-byte
// SERIALIZE_IGNORED m_id) as a leading Skip(4).
class hkpTypedBroadPhaseHandle : public IHavokObject {
public:
    std::int8_t   m_type = 0;                  // @4
    std::int8_t   m_objectQualityType = 0;     // @6
    std::uint32_t m_collisionFilterInfo = 0;   // @8
    HK_CLASS_ID(0xf4b0f799u, "hkpTypedBroadPhaseHandle")
};

// hkpCdBody — 32B, sig 0x54a4b841. Collision-detection body base (by-value base of hkpCollidable).
// m_shape ptr @0, m_shapeKey @8; two SERIALIZE_IGNORED null pointers (m_motion/m_parent) fill 16..31.
class hkpCdBody : public IHavokObject {
public:
    std::shared_ptr<hkpShape> m_shape;
    std::uint32_t             m_shapeKey = 0;
    HK_CLASS_ID(0x54a4b841u, "hkpCdBody")
};

// hkpMotion — 320B, sig 0x98aadb4f. Abstract rigid-body motion base; carries the motion state +
// velocity/inertia. Own fields @16; contains Vector4s → rounds to 16.
class hkpMotion : public hkReferencedObject {
public:
    std::uint8_t                 m_type = 0;                          // @16 MotionType
    std::uint8_t                 m_deactivationIntegrateCounter = 0;  // @17
    std::array<std::uint16_t, 2> m_deactivationNumInactiveFrames{};   // @18
    hkMotionState                m_motionState{};                     // @32 (176B)
    Vector4                      m_inertiaAndMassInv{};               // @208
    Vector4                      m_linearVelocity{};                  // @224
    Vector4                      m_angularVelocity{};                 // @240
    std::array<Vector4, 2>       m_deactivationRefPosition{};         // @256
    std::array<std::uint32_t, 2> m_deactivationRefOrientation{};      // @288
    std::uint16_t                m_savedQualityTypeIndex = 0;         // @304
    Half                         m_gravityFactor = 0;                 // @306 hkHalf raw
    HK_CLASS_ID(0x98aadb4fu, "hkpMotion")
};

// hkpKeyframedRigidMotion — 320B, sig 0xbafa2bb7. No own fields; pure delegation to hkpMotion.
class hkpKeyframedRigidMotion : public hkpMotion {
public:
    HK_CLASS_ID(0xbafa2bb7u, "hkpKeyframedRigidMotion")
};

// hkpMaxSizeMotion — 320B, sig 0x64abf85c. The "max footprint" motion Havok embeds by-value in every
// rigid body. No own fields.
class hkpMaxSizeMotion : public hkpKeyframedRigidMotion {
public:
    HK_CLASS_ID(0x64abf85cu, "hkpMaxSizeMotion")
};

// hkpCollidable — 112B, sig 0x9a0e42a5. Collision proxy over hkpCdBody. Own fields @32.
class hkpCollidable : public hkpCdBody {
public:
    std::uint8_t             m_forceCollideOntoPpu = 0;      // @33
    hkpTypedBroadPhaseHandle m_broadPhaseHandle{};           // @36 (12B by-value)
    float                    m_allowedPenetrationDepth = 0.f;// @104
    HK_CLASS_ID(0x9a0e42a5u, "hkpCollidable")
};

// hkpLinkedCollidable — 128B, sig 0xe1a81497. Adds one SERIALIZE_IGNORED collisionEntries hkArray.
class hkpLinkedCollidable : public hkpCollidable {
public:
    HK_CLASS_ID(0xe1a81497u, "hkpLinkedCollidable")
};

// hkpWorldObject — 208B, sig 0x49fb6f2e. Abstract physics-world object base. Own fields @16.
class hkpWorldObject : public hkReferencedObject {
public:
    std::uint64_t            m_userData = 0;      // @24 hkUlong
    hkpLinkedCollidable      m_collidable;        // @32 (128B by-value)
    hkMultiThreadCheck       m_multiThreadCheck;  // @160 (12B by-value)
    std::string              m_name;              // @176
    std::vector<hkpProperty> m_properties;        // @184 hkArray<hkpProperty>
    HK_CLASS_ID(0x49fb6f2eu, "hkpWorldObject")
};

// hkpEntity — 720B, sig 0xa03c774b. Abstract physics-entity base (concrete leaf: hkpRigidBody).
// Own fields @208. Many SERIALIZE_IGNORED runtime slots skipped; the 320B motion dominates.
class hkpEntity : public hkpWorldObject {
public:
    hkpMaterial                         m_material{};                                // @208
    float                               m_damageMultiplier = 0.f;                    // @232
    std::uint16_t                       m_storageIndex = 0;                          // @252
    std::uint16_t                       m_contactPointCallbackDelay = 0;             // @254
    std::int8_t                         m_autoRemoveLevel = 0;                       // @312
    std::uint8_t                        m_numShapeKeysInContactPointProperties = 0;  // @313
    std::uint8_t                        m_responseModifierFlags = 0;                 // @314
    std::uint32_t                       m_uid = 0;                                   // @316
    hkpEntitySpuCollisionCallback       m_spuCollisionCallback{};                    // @320
    hkpMaxSizeMotion                    m_motion{};                                  // @336 (320B)
    std::shared_ptr<hkReferencedObject> m_localFrame;                                // @688 hkLocalFrame*
    std::uint32_t                       m_npData = 0;                                // @704
    HK_CLASS_ID(0xa03c774bu, "hkpEntity")
};

// hkpRigidBody — 720B, sig 0x75f8d805. Concrete leaf; no own fields (all hkpEntity).
class hkpRigidBody : public hkpEntity {
public:
    HK_CLASS_ID(0x75f8d805u, "hkpRigidBody")
};

// hkpShapeInfo — 128B, sig 0xea7f1d08. A shape reference + child name/transform tables + a root
// transform. Own fields @16.
class hkpShapeInfo : public hkReferencedObject {
public:
    std::shared_ptr<hkpShape> m_shape;                          // @16
    bool                      m_isHierarchicalCompound = false; // @24
    bool                      m_hkdShapesCollected     = false; // @25
    std::vector<std::string>  m_childShapeNames;                // @32 hkArray<hkStringPtr>
    std::vector<hkTransform>  m_childTransforms;                // @48 hkArray<hkTransform>
    hkTransform               m_transform;                      // @64 (64B by-value)
    HK_CLASS_ID(0xea7f1d08u, "hkpShapeInfo")
};

// ── Constraints, motors, physics systems, ragdoll + skeleton mapper ──────────────
// The ragdoll rig's joints. Constraint DATA (hkpRagdollConstraintData / hkpLimitedHingeConstraintData)
// is one big by-value "atoms" block; each atom is an inline value struct (: IHavokObject, no base Read),
// every atom's first field is m_type (u16, serialized). Only the concrete top-level objects are REG'd.

// hkpConstraintData — 24B, sig 0x80559a4e. Abstract base (hkReferencedObject + m_userData). The
// ConstraintType enum has zero serialized width (genuinely ignored) — no skip for it.
class hkpConstraintData : public hkReferencedObject {
public:
    std::uint64_t m_userData = 0;   // @16 hkUlong
    HK_CLASS_ID(0x80559a4eu, "hkpConstraintData")
};

// hkpConstraintMotor — 24B, sig 0x6a44c317. Abstract motor base. m_type enum @16 + pad to 24.
class hkpConstraintMotor : public hkReferencedObject {
public:
    std::int8_t m_type = 0;   // @16 hkEnum<MotorType, hkInt8>
    HK_CLASS_ID(0x6a44c317u, "hkpConstraintMotor")
};

// hkpLimitedForceConstraintMotor — 32B, sig 0x3377b0b0. Abstract; adds the force clamp.
class hkpLimitedForceConstraintMotor : public hkpConstraintMotor {
public:
    float m_minForce = 0.f;   // @24
    float m_maxForce = 0.f;   // @28
    HK_CLASS_ID(0x3377b0b0u, "hkpLimitedForceConstraintMotor")
};

// hkpPositionConstraintMotor — 48B, sig 0x748fb303. Concrete PD position motor.
class hkpPositionConstraintMotor : public hkpLimitedForceConstraintMotor {
public:
    float m_tau                          = 0.f;   // @32
    float m_damping                      = 0.f;   // @36
    float m_proportionalRecoveryVelocity = 0.f;   // @40
    float m_constantRecoveryVelocity     = 0.f;   // @44
    HK_CLASS_ID(0x748fb303u, "hkpPositionConstraintMotor")
};

// ── constraint atoms (inline value structs) ─────────────────────────────────────
// hkpSetLocalTransformsConstraintAtom — 144B, sig 0x6e2a5198. m_type + 2 hkTransform pivot frames.
class hkpSetLocalTransformsConstraintAtom : public IHavokObject {
public:
    std::uint16_t m_type = 0;      // @0
    hkTransform   m_transformA{};  // @16 (64B)
    hkTransform   m_transformB{};  // @80 (64B)
    HK_CLASS_ID(0x6e2a5198u, "hkpSetLocalTransformsConstraintAtom")
};

// hkpSetupStabilizationAtom — 16B, sig 0xf05d137e. m_padding is a real serialized [u8;8].
class hkpSetupStabilizationAtom : public IHavokObject {
public:
    std::uint16_t              m_type = 0;       // @0
    bool                       m_enabled = false;// @2
    float                      m_maxAngle = 0.f; // @4
    std::array<std::uint8_t,8> m_padding{};      // @8
    HK_CLASS_ID(0xf05d137eu, "hkpSetupStabilizationAtom")
};

// hkpAngMotorConstraintAtom — 24B, sig 0x81f087ff. m_motor is a hkpConstraintMotor* (base-typed).
class hkpAngMotorConstraintAtom : public IHavokObject {
public:
    std::uint16_t m_type = 0;                                    // @0
    bool          m_isEnabled = false;                          // @2
    std::uint8_t  m_motorAxis = 0;                              // @3
    std::int16_t  m_initializedOffset = 0;                      // @4
    std::int16_t  m_previousTargetAngleOffset = 0;              // @6
    std::int16_t  m_correspondingAngLimitSolverResultOffset = 0;// @8
    float         m_targetAngle = 0.f;                          // @12
    std::shared_ptr<hkReferencedObject> m_motor;               // @16 hkpConstraintMotor*
    HK_CLASS_ID(0x81f087ffu, "hkpAngMotorConstraintAtom")
};

// hkpAngFrictionConstraintAtom — 12B, sig 0xf313aa80.
class hkpAngFrictionConstraintAtom : public IHavokObject {
public:
    std::uint16_t m_type = 0;                 // @0
    std::uint8_t  m_isEnabled = 0;            // @2
    std::uint8_t  m_firstFrictionAxis = 0;    // @3
    std::uint8_t  m_numFrictionAxes = 0;      // @4
    float         m_maxFrictionTorque = 0.f;  // @8
    HK_CLASS_ID(0xf313aa80u, "hkpAngFrictionConstraintAtom")
};

// hkpAngLimitConstraintAtom — 16B, sig 0x09be0d9d.
class hkpAngLimitConstraintAtom : public IHavokObject {
public:
    std::uint16_t m_type = 0;                    // @0
    std::uint8_t  m_isEnabled = 0;               // @2
    std::uint8_t  m_limitAxis = 0;               // @3
    float         m_minAngle = 0.f;              // @4
    float         m_maxAngle = 0.f;              // @8
    float         m_angularLimitsTauFactor = 0.f;// @12
    HK_CLASS_ID(0x09be0d9du, "hkpAngLimitConstraintAtom")
};

// hkp2dAngConstraintAtom — 4B, sig 0xdcdb8b8b.
class hkp2dAngConstraintAtom : public IHavokObject {
public:
    std::uint16_t m_type = 0;              // @0
    std::uint8_t  m_freeRotationAxis = 0;  // @2
    HK_CLASS_ID(0xdcdb8b8bu, "hkp2dAngConstraintAtom")
};

// hkpBallSocketConstraintAtom — 16B, sig 0xe70e4dfa.
class hkpBallSocketConstraintAtom : public IHavokObject {
public:
    std::uint16_t m_type = 0;                          // @0
    std::uint8_t  m_solvingMethod = 0;                 // @2
    std::uint8_t  m_bodiesToNotify = 0;                // @3
    std::uint8_t  m_velocityStabilizationFactor = 0;   // @4 hkUFloat8 raw
    float         m_maxImpulse = 0.f;                  // @8
    float         m_inertiaStabilizationFactor = 0.f;  // @12
    HK_CLASS_ID(0xe70e4dfau, "hkpBallSocketConstraintAtom")
};

// hkpLimitedHingeConstraintDataAtoms — 240B, sig 0x54c7715b. Fixed atom sequence.
class hkpLimitedHingeConstraintDataAtoms : public IHavokObject {
public:
    hkpSetLocalTransformsConstraintAtom m_transforms{};         // @0   (144B)
    hkpSetupStabilizationAtom           m_setupStabilization{}; // @144 (16B)
    hkpAngMotorConstraintAtom           m_angMotor{};           // @160 (24B)
    hkpAngFrictionConstraintAtom        m_angFriction{};        // @184 (12B)
    hkpAngLimitConstraintAtom           m_angLimit{};           // @196 (16B)
    hkp2dAngConstraintAtom              m_2dAng{};              // @212 (4B)
    hkpBallSocketConstraintAtom         m_ballSocket{};         // @216 (16B)
    HK_CLASS_ID(0x54c7715bu, "hkpLimitedHingeConstraintDataAtoms")
};

// hkpLimitedHingeConstraintData — 272B, sig 0x7c15bb6b. base + Skip(8) + 240B atoms.
class hkpLimitedHingeConstraintData : public hkpConstraintData {
public:
    hkpLimitedHingeConstraintDataAtoms m_atoms{};   // @32 (240B)
    HK_CLASS_ID(0x7c15bb6bu, "hkpLimitedHingeConstraintData")
};

// ── ragdoll-specific atoms ───────────────────────────────────────────────────────
// hkpRagdollMotorConstraintAtom — 96B, sig 0x71013826. Per-axis motors + target frame.
class hkpRagdollMotorConstraintAtom : public IHavokObject {
public:
    std::uint16_t          m_type = 0;                      // @0
    bool                   m_isEnabled = false;             // @2
    std::int16_t           m_initializedOffset = 0;         // @4 runtime scratch (serialized)
    std::int16_t           m_previousTargetAnglesOffset = 0;// @6 runtime scratch (serialized)
    std::array<Vector4, 3> m_targetBRca{};                  // @16 hkMatrix3 (48B)
    std::array<std::shared_ptr<hkReferencedObject>, 3> m_motors{}; // @64 hkpConstraintMotor*[3]
    HK_CLASS_ID(0x71013826u, "hkpRagdollMotorConstraintAtom")
};

// hkpTwistLimitConstraintAtom — 20B, sig 0x7c9b1052.
class hkpTwistLimitConstraintAtom : public IHavokObject {
public:
    std::uint16_t m_type = 0;                     // @0
    std::uint8_t  m_isEnabled = 0;                // @2
    std::uint8_t  m_twistAxis = 0;                // @3
    std::uint8_t  m_refAxis = 0;                  // @4
    float         m_minAngle = 0.f;               // @8
    float         m_maxAngle = 0.f;               // @12
    float         m_angularLimitsTauFactor = 0.f; // @16
    HK_CLASS_ID(0x7c9b1052u, "hkpTwistLimitConstraintAtom")
};

// hkpConeLimitConstraintAtom — 20B, sig 0xf19443c8. Reused for both coneLimit and planesLimit.
class hkpConeLimitConstraintAtom : public IHavokObject {
public:
    std::uint16_t m_type = 0;                     // @0
    std::uint8_t  m_isEnabled = 0;                // @2
    std::uint8_t  m_twistAxisInA = 0;             // @3
    std::uint8_t  m_refAxisInB = 0;               // @4
    std::uint8_t  m_angleMeasurementMode = 0;     // @5
    std::uint8_t  m_memOffsetToAngleOffset = 0;   // @6
    float         m_minAngle = 0.f;               // @8
    float         m_maxAngle = 0.f;               // @12
    float         m_angularLimitsTauFactor = 0.f; // @16
    HK_CLASS_ID(0xf19443c8u, "hkpConeLimitConstraintAtom")
};

// hkpRagdollConstraintDataAtoms — 352B, sig 0xeed76b00. Fixed atom sequence.
class hkpRagdollConstraintDataAtoms : public IHavokObject {
public:
    hkpSetLocalTransformsConstraintAtom m_transforms{};         // @0   (144B)
    hkpSetupStabilizationAtom           m_setupStabilization{}; // @144 (16B)
    hkpRagdollMotorConstraintAtom       m_ragdollMotors{};      // @160 (96B)
    hkpAngFrictionConstraintAtom        m_angFriction{};        // @256 (12B)
    hkpTwistLimitConstraintAtom         m_twistLimit{};         // @268 (20B)
    hkpConeLimitConstraintAtom          m_coneLimit{};          // @288 (20B)
    hkpConeLimitConstraintAtom          m_planesLimit{};        // @308 (20B, same type)
    hkpBallSocketConstraintAtom         m_ballSocket{};         // @328 (16B) -> @344
    HK_CLASS_ID(0xeed76b00u, "hkpRagdollConstraintDataAtoms")
};

// hkpRagdollConstraintData — 384B, sig 0x8fb5dd29. base + Skip(8) + 352B atoms.
class hkpRagdollConstraintData : public hkpConstraintData {
public:
    hkpRagdollConstraintDataAtoms m_atoms{};   // @32 (352B)
    HK_CLASS_ID(0x8fb5dd29u, "hkpRagdollConstraintData")
};

// hkpConstraintInstance — 112B, sig 0x034eba5f. Binds two entities to constraint data.
class hkpConstraintInstance : public hkReferencedObject {
public:
    std::shared_ptr<hkReferencedObject>       m_data;                     // @24 hkpConstraintData*
    std::shared_ptr<hkReferencedObject>       m_constraintModifiers;      // @32 hkpModifierConstraintAtom*
    std::array<std::shared_ptr<hkpEntity>, 2> m_entities{};               // @40 hkpEntity*[2]
    std::uint8_t                              m_priority = 0;             // @56
    bool                                      m_wantRuntime = false;      // @57
    std::uint8_t                              m_destructionRemapInfo = 0; // @58
    std::string                               m_name;                     // @80
    std::uint64_t                             m_userData = 0;             // @88
    HK_CLASS_ID(0x034eba5fu, "hkpConstraintInstance")
};

// hkpPhysicsSystem — 104B, sig 0xff724c17. Container of rigid bodies + constraints + actions + phantoms.
class hkpPhysicsSystem : public hkReferencedObject {
public:
    std::vector<std::shared_ptr<hkpRigidBody>>       m_rigidBodies;   // @16
    std::vector<std::shared_ptr<hkReferencedObject>> m_constraints;   // @32 hkpConstraintInstance*
    std::vector<std::shared_ptr<hkReferencedObject>> m_actions;       // @48 hkpAction*
    std::vector<std::shared_ptr<hkReferencedObject>> m_phantoms;      // @64 hkpPhantom*
    std::string                                      m_name;          // @80
    std::uint64_t                                    m_userData = 0;  // @88
    bool                                             m_active = false;// @96
    HK_CLASS_ID(0xff724c17u, "hkpPhysicsSystem")
};

// hkpPhysicsData — 40B, sig 0xc2a461e4. Top-level physics container (worldCinfo + systems array).
class hkpPhysicsData : public hkReferencedObject {
public:
    std::shared_ptr<hkReferencedObject>            m_worldCinfo;   // @16 hkpWorldCinfo* (base-typed)
    std::vector<std::shared_ptr<hkpPhysicsSystem>> m_systems;      // @24 hkArray<hkpPhysicsSystem*>
    HK_CLASS_ID(0xc2a461e4u, "hkpPhysicsData")
};

// hkaRagdollInstance — 72B, sig 0x154948e8. Ragdoll rig: rigid bodies + constraints + bone map + skeleton.
class hkaRagdollInstance : public hkReferencedObject {
public:
    std::vector<std::shared_ptr<hkpRigidBody>>       m_rigidBodies;        // @16
    std::vector<std::shared_ptr<hkReferencedObject>> m_constraints;        // @32 hkpConstraintInstance*
    std::vector<std::int32_t>                        m_boneToRigidBodyMap; // @48 hkArray<hkInt32>
    std::shared_ptr<hkaSkeleton>                     m_skeleton;           // @64
    HK_CLASS_ID(0x154948e8u, "hkaRagdollInstance")
};

// ── skeleton mapper (retarget data) ─────────────────────────────────────────────
// hkaSkeletonMapperDataSimpleMapping — 64B, sig 0x3405deca.
class hkaSkeletonMapperDataSimpleMapping : public IHavokObject {
public:
    std::int16_t m_boneA = 0;           // @0
    std::int16_t m_boneB = 0;           // @2
    QSTransform  m_aFromBTransform{};   // @16 (48B)
    HK_CLASS_ID(0x3405decau, "hkaSkeletonMapperDataSimpleMapping")
};

// hkaSkeletonMapperDataChainMapping — 112B, sig 0xa528f7cf.
class hkaSkeletonMapperDataChainMapping : public IHavokObject {
public:
    std::int16_t m_startBoneA = 0;              // @0
    std::int16_t m_endBoneA = 0;                // @2
    std::int16_t m_startBoneB = 0;              // @4
    std::int16_t m_endBoneB = 0;                // @6
    QSTransform  m_startAFromBTransform{};      // @16 (48B)
    QSTransform  m_endAFromBTransform{};        // @64 (48B)
    HK_CLASS_ID(0xa528f7cfu, "hkaSkeletonMapperDataChainMapping")
};

// hkaSkeletonMapperData — 128B, sig 0x95687ea0.
class hkaSkeletonMapperData : public IHavokObject {
public:
    std::shared_ptr<hkaSkeleton>                    m_skeletonA;                 // @0
    std::shared_ptr<hkaSkeleton>                    m_skeletonB;                 // @8
    std::vector<hkaSkeletonMapperDataSimpleMapping> m_simpleMappings;            // @16
    std::vector<hkaSkeletonMapperDataChainMapping>  m_chainMappings;             // @32
    std::vector<std::int16_t>                       m_unmappedBones;             // @48
    QSTransform                                     m_extractedMotionMapping{};  // @64 (48B)
    bool                                            m_keepUnmappedLocal = false; // @112
    std::uint32_t                                   m_mappingType = 0;           // @116
    HK_CLASS_ID(0x95687ea0u, "hkaSkeletonMapperData")
};

// hkaSkeletonMapper — 144B, sig 0x12df42a5. base + 128B mapping data.
class hkaSkeletonMapper : public hkReferencedObject {
public:
    hkaSkeletonMapperData m_mapping{};   // @16 (128B)
    HK_CLASS_ID(0x12df42a5u, "hkaSkeletonMapper")
};

} // namespace havok
