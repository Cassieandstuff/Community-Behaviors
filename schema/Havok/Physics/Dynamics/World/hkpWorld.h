/// @file
/// @brief hkpWorld: only what the behavior runtime calls.
#pragma once

#include <Physics/Collide/Filter/Group/hkpGroupFilter.h>

namespace CB { namespace schema
{
    class hkpWorld : public hkReferencedObject
    {
    public:
        char                m_pad10[0xd0 - 0x10];
        hkpCollisionFilter* m_collisionFilter;   ///< +0xd0

        /// @brief Takes / releases the world lock around reading a body's properties.
        /// @note Names provisional. Skyrim's versions enter/leave a global critical section and count in +0x130.
        void lock();
        void unlock();
    };
} }
