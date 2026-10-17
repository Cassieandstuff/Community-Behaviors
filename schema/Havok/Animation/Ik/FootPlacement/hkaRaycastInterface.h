/// @file
/// @brief hkaRaycastInterface: casts the rays foot placement and the behavior nodes ask for (the game implements it).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 8 bytes (the vtable). In the Havok SDK the class sits beside hkaFootPlacementIkSolver; the exe has no
    ///       hkbRaycastInterface (the behavior runtime's raycast interface IS this class: hkbDefaultRaycastInterface's
    ///       RTTI lists hkaRaycastInterface as its only other base).
    /// @warning MSVC groups overloaded virtuals and lays them out in reverse declaration order: declared in this order,
    ///          the filtered castRay is slot 1 and the unfiltered one slot 2, as in vanilla's vtable.
    class hkaRaycastInterface
    {
    public:
        /// @brief Returns the block to the thread's heap with the class's static size.
        HK_FORCE_INLINE void operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }

        virtual ~hkaRaycastInterface() {}   ///< slot 0

        /// @brief Whether the ray from `fromWS` to `toWS` hits; the hit's fraction along it and its normal.
        virtual hkBool castRay(const hkVector4& fromWS, const hkVector4& toWS, hkReal& hitFractionOut, hkVector4& normalWSOut)   ///< slot 2
        {
            return false;
        }
        /// @brief castRay against what `collisionFilterInfo` collides with.
        virtual hkBool castRay(const hkVector4& fromWS, const hkVector4& toWS, hkUint32 collisionFilterInfo, hkReal& hitFractionOut,
                               hkVector4& normalWSOut)   ///< slot 1
        {
            return false;
        }
    };
} }
