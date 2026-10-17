/// @file
/// @brief hkpWorldPostSimulationListener: told after each world simulation step.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpWorld;
    class hkpEntity;

    /// @note 8 bytes (the vtable). Slot 1 is proven by hkpCharacterRigidBody's override; slot 2 is the Havok SDK's.
    class hkpWorldPostSimulationListener
    {
    public:
        virtual ~hkpWorldPostSimulationListener() {}
        virtual void postSimulationCallback(hkpWorld* world) = 0;          ///< slot 1
        virtual void inactiveEntityMovedCallback(hkpEntity* entity) {}     ///< slot 2
    };
} }
