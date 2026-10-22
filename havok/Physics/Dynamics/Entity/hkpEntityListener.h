/// @file
/// @brief hkpEntityListener: told when an entity is added to, removed from or deleted with a world.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkpEntity;

    /// @note 8 bytes (the vtable). Slots 1 and 2 are proven by hkpCharacterRigidBody's overrides; 3..5 are the Havok SDK's
    ///       (vanilla's vtable points them at empty bodies). hkpCharacterProxy derives from it too (at +0x10).
    class hkpEntityListener
    {
    public:
        virtual ~hkpEntityListener() {}
        virtual void entityAddedCallback(hkpEntity* entity) {}           ///< slot 1
        virtual void entityRemovedCallback(hkpEntity* entity) {}         ///< slot 2
        virtual void entityShapeSetCallback(hkpEntity* entity) {}        ///< slot 3
        virtual void entitySetMotionTypeCallback(hkpEntity* entity) {}   ///< slot 4
        virtual void entityDeletedCallback(hkpEntity* entity) {}         ///< slot 5
    };
} }
