/// @file
/// @brief hkpContactPointProperties: a contact point's solver results and material.
#pragma once

#include <Common/Base/Types/Physics/ContactPoint/hkContactPointMaterial.h>

namespace CB { namespace havok
{
    /// @note 8 bytes (the material follows at +0x08 in hkpContactPointProperties); members not proven here.
    class hkpSolverResults
    {
    public:
        hkUint8 m_pad00[8];
    };

    /// @note The material's flags sit at +0x13 (hkpCharacterRigidBodyListener::considerCollisionEntryForSlope). Not
    ///       reflected; nothing past the material is declared.
    class hkpContactPointProperties : public hkpSolverResults, public hkContactPointMaterial
    {
    };
} }
