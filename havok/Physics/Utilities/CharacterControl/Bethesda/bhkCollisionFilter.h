/// @file
/// @brief bhkCollisionFilter: Bethesda's collision filter (Havok's group filter with the game's layer tables).
/// @note Not Havok: declared only as far as the character controller's code reads it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note Opaque up to the sensor field (the group filter and the layer tables are padding here).
    class bhkCollisionFilter
    {
    public:
        static HK_FORCE_INLINE bhkCollisionFilter* GetSingleton() { return s_singleton; }

        hkUint8  m_pad000[0x3d8];
        /// @brief One bit per collision layer: the proxy controller zeroes the plane velocity of a phantom contact on
        ///        a layer set here.
        /// @note Name per CommonLibSSE (sensorField).
        hkUlong  m_sensorField;   ///< +0x3d8

        /// @brief The game's filter.
        static bhkCollisionFilter* s_singleton;
    };
} }
