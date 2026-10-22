#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x18).
    class hkpConstraintMotor : public hkReferencedObject
    {
    public:
        enum MotorType
        {
            TYPE_INVALID = 0,
            TYPE_POSITION = 1,
            TYPE_VELOCITY = 2,
            TYPE_SPRING_DAMPER = 3,
            TYPE_CALLBACK = 4,
            TYPE_MAX = 5,
        };
        hkInt8 m_type;   ///< +0x10
    };
} }
