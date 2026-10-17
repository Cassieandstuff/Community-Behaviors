/// @file
/// @brief hkpMaterial: an entity's surface response, friction and restitution.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    /// @note 12 bytes as declared; schema/physics/hkpMaterial.yaml counts 16 because hkpEntity aligns the pointer after it.
    class hkpMaterial
    {
    public:
        hkInt8 m_responseType;                ///< +0x00 — hkpMaterial::ResponseType
        hkHalf m_rollingFrictionMultiplier;   ///< +0x02
        hkReal m_friction;                    ///< +0x04
        hkReal m_restitution;                 ///< +0x08
    };
} }
