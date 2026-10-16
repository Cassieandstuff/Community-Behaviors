/// @file
/// @brief hkpKeyframedRigidMotion and hkpMaxSizeMotion, the motion every entity embeds.
#pragma once

#include <Physics/Dynamics/Motion/hkpMotion.h>

namespace CB { namespace schema
{
    /// @note 0x140 bytes, no members of its own (schema/physics/hkpKeyframedRigidMotion.yaml).
    class hkpKeyframedRigidMotion : public hkpMotion
    {
    };

    /// @brief The motion every entity embeds: large enough to be switched in place to any motion type.
    /// @note 0x140 bytes (schema/physics/hkpMaxSizeMotion.yaml).
    class hkpMaxSizeMotion : public hkpKeyframedRigidMotion
    {
    };
} }
