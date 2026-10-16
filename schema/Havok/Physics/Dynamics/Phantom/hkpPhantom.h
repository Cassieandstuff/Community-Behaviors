/// @file
/// @brief hkpPhantom: a world object that reports overlaps but is not simulated.
#pragma once

#include <Physics/Dynamics/World/hkpWorldObject.h>

namespace CB { namespace schema
{
    class hkpPhantomOverlapListener;
    class hkpPhantomListener;

    /// @note 0xf0 bytes. Its collidable's broad-phase type is BROAD_PHASE_PHANTOM (2).
    class hkpPhantom : public hkpWorldObject
    {
    public:
        hkArray<hkpPhantomOverlapListener*> m_overlapListeners;   ///< +0xd0
        hkArray<hkpPhantomListener*>        m_phantomListeners;   ///< +0xe0
    };
} }
