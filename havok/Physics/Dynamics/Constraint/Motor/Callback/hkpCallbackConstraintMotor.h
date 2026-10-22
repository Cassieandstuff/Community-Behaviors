#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Motor/LimitedForce/hkpLimitedForceConstraintMotor.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x48).
    class hkpCallbackConstraintMotor : public hkpLimitedForceConstraintMotor
    {
    public:
        enum CallbackType
        {
            CALLBACK_MOTOR_TYPE_HAVOK_DEMO_SPRING_DAMPER = 0,
            CALLBACK_MOTOR_TYPE_USER_0 = 1,
            CALLBACK_MOTOR_TYPE_USER_1 = 2,
            CALLBACK_MOTOR_TYPE_USER_2 = 3,
            CALLBACK_MOTOR_TYPE_USER_3 = 4,
        };
        void* m_callbackFunc;   ///< +0x20, not serialized
        hkUint32 m_callbackType;   ///< +0x28
        hkUlong m_userData0;   ///< +0x30
        hkUlong m_userData1;   ///< +0x38
        hkUlong m_userData2;   ///< +0x40
    };
} }
