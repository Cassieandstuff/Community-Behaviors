/// @file
/// @brief hkpWorld: only what the behavior runtime calls.
#pragma once

#include <Physics/Collide/Filter/Group/hkpGroupFilter.h>

namespace CB { namespace schema
{
    class hkpRigidBody;

    /// @note Havok SDK enum; only success is named here.
    enum hkpStepResult { HK_STEP_RESULT_SUCCESS = 0 };

    class hkpWorld : public hkReferencedObject
    {
    public:
        char                m_pad10[0xd0 - 0x10];
        hkpCollisionFilter* m_collisionFilter;   ///< +0xd0

        /// @brief Takes / releases the world lock around reading a body's properties.
        /// @note Names provisional. Skyrim's versions enter/leave a global critical section and count in +0x130.
        void lock();
        void unlock();
        /// @brief Re-runs the collision filter over `entity`'s collision pairs after its filter info changed.
        /// @note Out of line in vanilla (hkp code, not reimplemented); parameters per the Havok SDK (an
        ///       hkpUpdateCollisionFilterOnEntityMode and an hkpUpdateCollectionFilterMode).
        void updateCollisionFilterOnEntity(hkpRigidBody* entity, int updateMode, int updateShapeCollectionFilter);
        /// @brief Advances the simulation by `physicsDeltaTime`.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        hkpStepResult stepDeltaTime(hkReal physicsDeltaTime);
    };
} }
