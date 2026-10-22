/// @file
/// @brief hkpConstraintData: the shared description of a constraint (its atoms), referenced by its instances.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x18 bytes (schema/physics/hkpConstraintData.yaml).
    class hkpConstraintData : public hkReferencedObject
    {
    public:
        hkUlong m_userData;   ///< +0x10
    };
} }
