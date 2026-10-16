/// @file
/// @brief hkbDefaultRaycastInterface, reimplemented against vanilla (AE 1.6.1170) — all but castRay.
#include <Behavior/Utils/hkbDefaultRaycastInterface.h>

namespace CB { namespace schema
{
    hkbDefaultRaycastInterface::hkbDefaultRaycastInterface(hkpWorld* world)
    :   m_world(world),
        m_minUpDotNormal(0.0f)
    {
    }

    void hkbDefaultRaycastInterface::setWorld(hkpWorld* world)
    {
        m_world = world;
    }
} }
