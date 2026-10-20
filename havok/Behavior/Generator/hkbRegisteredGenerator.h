/// @file
/// @brief hkbRegisteredGenerator: a generator registered with a placement relative to its owner.
#pragma once

#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace havok
{
    /// @note 0x60 bytes; layout per Havok's reflection.
    class hkbRegisteredGenerator : public hkbBindable
    {
    public:
        hkRefPtr<hkbGenerator> m_generator;           ///< +0x30
        hkVector4              m_relativePosition;    ///< +0x40
        hkVector4              m_relativeDirection;   ///< +0x50

        hkbRegisteredGenerator(hkFinishLoadedObjectFlag flag);
    };
} }
