#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

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
} }
