/// @file
/// @brief hkbDefaultRaycastInterface: the raycast interface that casts against a physics world.
#pragma once

#include <Animation/Ik/FootPlacement/hkaRaycastInterface.h>

namespace CB { namespace havok
{
    class hkpWorld;

    /// @note 0x28 bytes: hkReferencedObject, then hkaRaycastInterface at +0x10. The destructor is implicit (vanilla's
    ///       scalar deleting destructor stamps each base's vtable and nothing of its own).
    class hkbDefaultRaycastInterface : public hkReferencedObject, public hkaRaycastInterface
    {
    public:
        using hkReferencedObject::operator new;
        using hkReferencedObject::operator delete;

        /// @note Not owned.
        hkpWorld* m_world;          ///< +0x18
        /// @brief Hits whose normal is closer than this to straight down the world's gravity are dropped (0 = keep
        ///        every hit).
        /// @note Name provisional: castRay compares it with -normalize(gravity)·normal.
        hkReal    m_minUpDotNormal;   ///< +0x20

        hkbDefaultRaycastInterface(hkpWorld* world);

        /// @note Name provisional; out of line in vanilla.
        void setWorld(hkpWorld* world);

        /// @brief Casts against m_world with a closest-hit collector; a hit whose normal faces down against gravity
        ///        (up·normal < m_minUpDotNormal) is dropped, and a convex-translate root shape's radius is taken off the
        ///        hit fraction.
        /// @note Slot 1 of the hkaRaycastInterface vtable (0x140b102c0).
        virtual hkBool castRay(const hkVector4& fromWS, const hkVector4& toWS, hkUint32 collisionFilterInfo, hkReal& hitFractionOut,
                               hkVector4& normalWSOut);
    };
} }
