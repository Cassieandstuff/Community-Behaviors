#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x50, version 1).
    class hkxLight : public hkReferencedObject
    {
    public:
        enum LightType
        {
            POINT_LIGHT = 0,
            DIRECTIONAL_LIGHT = 1,
            SPOT_LIGHT = 2,
        };
        hkInt8 m_type;   ///< +0x10
        hkVector4 m_position;   ///< +0x20
        hkVector4 m_direction;   ///< +0x30
        hkUint32 m_color;   ///< +0x40
        hkReal m_angle;   ///< +0x44
    };
} }
