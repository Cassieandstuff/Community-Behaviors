#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>

namespace CB { namespace schema
{
    class hkpConstraintMotor;

    /// @brief Seeded from Havok's reflection (size 0x2).
    struct hkpConstraintAtom
    {
    public:
        enum AtomType
        {
            TYPE_INVALID = 0,
            TYPE_BRIDGE = 1,
            TYPE_SET_LOCAL_TRANSFORMS = 2,
            TYPE_SET_LOCAL_TRANSLATIONS = 3,
            TYPE_SET_LOCAL_ROTATIONS = 4,
            TYPE_BALL_SOCKET = 5,
            TYPE_STIFF_SPRING = 6,
            TYPE_LIN = 7,
            TYPE_LIN_SOFT = 8,
            TYPE_LIN_LIMIT = 9,
            TYPE_LIN_FRICTION = 10,
            TYPE_LIN_MOTOR = 11,
            TYPE_2D_ANG = 12,
            TYPE_ANG = 13,
            TYPE_ANG_LIMIT = 14,
            TYPE_TWIST_LIMIT = 15,
            TYPE_CONE_LIMIT = 16,
            TYPE_ANG_FRICTION = 17,
            TYPE_ANG_MOTOR = 18,
            TYPE_RAGDOLL_MOTOR = 19,
            TYPE_PULLEY = 20,
            TYPE_RACK_AND_PINION = 21,
            TYPE_COG_WHEEL = 22,
            TYPE_SETUP_STABILIZATION = 23,
            TYPE_OVERWRITE_PIVOT = 24,
            TYPE_CONTACT = 25,
            TYPE_MODIFIER_SOFT_CONTACT = 26,
            TYPE_MODIFIER_MASS_CHANGER = 27,
            TYPE_MODIFIER_VISCOUS_SURFACE = 28,
            TYPE_MODIFIER_MOVING_SURFACE = 29,
            TYPE_MODIFIER_IGNORE_CONSTRAINT = 30,
            TYPE_MODIFIER_CENTER_OF_MASS_CHANGER = 31,
            TYPE_MAX = 32,
        };
        enum CallbackRequest
        {
            CALLBACK_REQUEST_NONE = 0,
            CALLBACK_REQUEST_NEW_CONTACT_POINT = 1,
            CALLBACK_REQUEST_SETUP_PPU_ONLY = 2,
            CALLBACK_REQUEST_SETUP_CALLBACK = 4,
            CALLBACK_REQUEST_CONTACT_POINT_CALLBACK = 8,
        };
        enum SolvingMethod
        {
            METHOD_STABILIZED = 0,
            METHOD_OLD = 1,
        };
        hkUint16 m_type;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x4).
    struct hkp2dAngConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_freeRotationAxis;   ///< +0x2
    };

    /// @brief Seeded from Havok's reflection (size 0xc).
    struct hkpAngFrictionConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_isEnabled;   ///< +0x2
        hkUint8 m_firstFrictionAxis;   ///< +0x3
        hkUint8 m_numFrictionAxes;   ///< +0x4
        hkReal m_maxFrictionTorque;   ///< +0x8
    };

    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkpAngLimitConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_isEnabled;   ///< +0x2
        hkUint8 m_limitAxis;   ///< +0x3
        hkReal m_minAngle;   ///< +0x4
        hkReal m_maxAngle;   ///< +0x8
        hkReal m_angularLimitsTauFactor;   ///< +0xc
    };

    /// @brief Seeded from Havok's reflection (size 0x18).
    struct hkpAngMotorConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkBool m_isEnabled;   ///< +0x2
        hkUint8 m_motorAxis;   ///< +0x3
        hkInt16 m_initializedOffset;   ///< +0x4
        hkInt16 m_previousTargetAngleOffset;   ///< +0x6
        hkInt16 m_correspondingAngLimitSolverResultOffset;   ///< +0x8
        hkReal m_targetAngle;   ///< +0xc
        hkpConstraintMotor* m_motor;   ///< +0x10
    };

    /// @brief Seeded from Havok's reflection (size 0x10, version 3).
    struct hkpBallSocketConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_solvingMethod;   ///< +0x2
        hkUint8 m_bodiesToNotify;   ///< +0x3
        hkUint8 m_velocityStabilizationFactor;   ///< +0x4
        hkReal m_maxImpulse;   ///< +0x8
        hkReal m_inertiaStabilizationFactor;   ///< +0xc
    };

    /// @brief Seeded from Havok's reflection (size 0x14).
    struct hkpConeLimitConstraintAtom : public hkpConstraintAtom
    {
    public:
        enum MeasurementMode
        {
            ZERO_WHEN_VECTORS_ALIGNED = 0,
            ZERO_WHEN_VECTORS_PERPENDICULAR = 1,
        };
        hkUint8 m_isEnabled;   ///< +0x2
        hkUint8 m_twistAxisInA;   ///< +0x3
        hkUint8 m_refAxisInB;   ///< +0x4
        hkUint8 m_angleMeasurementMode;   ///< +0x5
        hkUint8 m_memOffsetToAngleOffset;   ///< +0x6
        hkReal m_minAngle;   ///< +0x8
        hkReal m_maxAngle;   ///< +0xc
        hkReal m_angularLimitsTauFactor;   ///< +0x10
    };

    /// @brief Seeded from Havok's reflection (size 0x60).
    struct hkpRagdollMotorConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkBool m_isEnabled;   ///< +0x2
        hkInt16 m_initializedOffset;   ///< +0x4
        hkInt16 m_previousTargetAnglesOffset;   ///< +0x6
        hkVector4 m_pad0x10[3];   ///< target_bRca (MATRIX3, a type the schema lacks)
        hkpConstraintMotor* m_motors[3];   ///< +0x40
    };

    /// @brief Seeded from Havok's reflection (size 0x90).
    struct hkpSetLocalTransformsConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkTransform m_transformA;   ///< +0x10
        hkTransform m_transformB;   ///< +0x50
    };

    /// @brief Seeded from Havok's reflection (size 0x10, version 1).
    struct hkpSetupStabilizationAtom : public hkpConstraintAtom
    {
    public:
        hkBool m_enabled;   ///< +0x2
        hkReal m_maxAngle;   ///< +0x4
        hkUint8 m_padding[8];   ///< +0x8
    };

    /// @brief Seeded from Havok's reflection (size 0x14).
    struct hkpTwistLimitConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_isEnabled;   ///< +0x2
        hkUint8 m_twistAxis;   ///< +0x3
        hkUint8 m_refAxis;   ///< +0x4
        hkReal m_minAngle;   ///< +0x8
        hkReal m_maxAngle;   ///< +0xc
        hkReal m_angularLimitsTauFactor;   ///< +0x10
    };

    /// @brief Seeded from Havok's reflection (size 0x4).
    struct hkpAngConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_firstConstrainedAxis;   ///< +0x2
        hkUint8 m_numConstrainedAxes;   ///< +0x3
    };

    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkpCogWheelConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkReal m_cogWheelRadiusA;   ///< +0x4
        hkReal m_cogWheelRadiusB;   ///< +0x8
        hkBool m_isScrew;   ///< +0xc
        hkInt8 m_memOffsetToInitialAngleOffset;   ///< +0xd
        hkInt8 m_memOffsetToPrevAngle;   ///< +0xe
        hkInt8 m_memOffsetToRevolutionCounter;   ///< +0xf
    };

    /// @brief Seeded from Havok's reflection (size 0x4).
    struct hkpLinConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_axisIndex;   ///< +0x2
    };

    /// @brief Seeded from Havok's reflection (size 0x8).
    struct hkpLinFrictionConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_isEnabled;   ///< +0x2
        hkUint8 m_frictionAxis;   ///< +0x3
        hkReal m_maxFrictionForce;   ///< +0x4
    };

    /// @brief Seeded from Havok's reflection (size 0xc).
    struct hkpLinLimitConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_axisIndex;   ///< +0x2
        hkReal m_min;   ///< +0x4
        hkReal m_max;   ///< +0x8
    };

    /// @brief Seeded from Havok's reflection (size 0x18).
    struct hkpLinMotorConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkBool m_isEnabled;   ///< +0x2
        hkUint8 m_motorAxis;   ///< +0x3
        hkInt16 m_initializedOffset;   ///< +0x4
        hkInt16 m_previousTargetPositionOffset;   ///< +0x6
        hkReal m_targetPosition;   ///< +0x8
        hkpConstraintMotor* m_motor;   ///< +0x10
    };

    /// @brief Seeded from Havok's reflection (size 0xc).
    struct hkpLinSoftConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_axisIndex;   ///< +0x2
        hkReal m_tau;   ///< +0x4
        hkReal m_damping;   ///< +0x8
    };

    /// @brief Seeded from Havok's reflection (size 0x4).
    struct hkpOverwritePivotConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8 m_copyToPivotBFromPivotA;   ///< +0x2
    };

    /// @brief Seeded from Havok's reflection (size 0x40).
    struct hkpPulleyConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkVector4 m_fixedPivotAinWorld;   ///< +0x10
        hkVector4 m_fixedPivotBinWorld;   ///< +0x20
        hkReal m_ropeLength;   ///< +0x30
        hkReal m_leverageOnBodyB;   ///< +0x34
    };

    /// @brief Seeded from Havok's reflection (size 0xc).
    struct hkpRackAndPinionConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkReal m_pinionRadiusOrScrewPitch;   ///< +0x4
        hkBool m_isScrew;   ///< +0x8
        hkInt8 m_memOffsetToInitialAngleOffset;   ///< +0x9
        hkInt8 m_memOffsetToPrevAngle;   ///< +0xa
        hkInt8 m_memOffsetToRevolutionCounter;   ///< +0xb
    };

    /// @brief Seeded from Havok's reflection (size 0x70).
    struct hkpSetLocalRotationsConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkRotation m_rotationA;   ///< +0x10
        hkRotation m_rotationB;   ///< +0x40
    };

    /// @brief Seeded from Havok's reflection (size 0x30).
    struct hkpSetLocalTranslationsConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkVector4 m_translationA;   ///< +0x10
        hkVector4 m_translationB;   ///< +0x20
    };

    /// @brief Seeded from Havok's reflection (size 0x8).
    struct hkpStiffSpringConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkReal m_length;   ///< +0x4
    };

    /// @brief An atom a contact constraint's modifier chain links in (mass changer, soft contact, ...).
    /// @note Per Havok's reflection (size 0x30); the constructor is the one the modifiers' inline constructors chain to.
    struct __declspec(align(16)) hkpModifierConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint8            m_pad02[0x10 - 0x02];
        hkUint16           m_modifierAtomSize;   ///< +0x10
        hkUint16           m_childSize;          ///< +0x12
        hkUint8            m_pad14[0x18 - 0x14];
        hkpConstraintAtom* m_child;              ///< +0x18
        hkUint32           m_pad[2];             ///< +0x20
        hkUint8            m_pad28[0x30 - 0x28];

        HK_FORCE_INLINE hkpModifierConstraintAtom(AtomType type, int size)
        {
            m_type             = static_cast<hkUint16>(type);
            m_modifierAtomSize = static_cast<hkUint16>(size);
        }
    };

    /// @brief Seeded from Havok's reflection (size 0x50).
    struct hkpCenterOfMassChangerModifierConstraintAtom : public hkpModifierConstraintAtom
    {
    public:
        hkVector4 m_displacementA;   ///< +0x30
        hkVector4 m_displacementB;   ///< +0x40
    };

    /// @brief Seeded from Havok's reflection (size 0x30).
    struct hkpIgnoreModifierConstraintAtom : public hkpModifierConstraintAtom
    {
    public:
    };

    /// @brief Scales the inverse masses (xyz: inertia, w: mass) of a contact's two bodies.
    /// @note Per Havok's reflection (size 0x50).
    struct hkpMassChangerModifierConstraintAtom : public hkpModifierConstraintAtom
    {
    public:
        hkVector4 m_factorA;   ///< +0x30
        hkVector4 m_factorB;   ///< +0x40

        HK_FORCE_INLINE hkpMassChangerModifierConstraintAtom() : hkpModifierConstraintAtom(TYPE_MODIFIER_MASS_CHANGER, sizeof(hkpMassChangerModifierConstraintAtom)) {}

        /// @brief The heap's block allocator.
        /// @note `throw()`: vanilla null-checks the block before constructing in it.
        HK_FORCE_INLINE void* operator new(hkUlong nbytes) throw() { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }
    };

    /// @brief Seeded from Havok's reflection (size 0x40).
    struct hkpMovingSurfaceModifierConstraintAtom : public hkpModifierConstraintAtom
    {
    public:
        hkVector4 m_velocity;   ///< +0x30
    };

    /// @brief Seeded from Havok's reflection (size 0x40).
    struct hkpSoftContactModifierConstraintAtom : public hkpModifierConstraintAtom
    {
    public:
        hkReal m_tau;   ///< +0x30
        hkReal m_maxAcceleration;   ///< +0x34
        hkUint8 m_pad0x38[0x8];
    };

    /// @brief Seeded from Havok's reflection (size 0x30).
    struct hkpViscousSurfaceModifierConstraintAtom : public hkpModifierConstraintAtom
    {
    public:
    };
} }
