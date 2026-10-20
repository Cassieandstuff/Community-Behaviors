/// @file
/// @brief hkaRagdollRaycastInterface: the ray cast hkaDetectRagdollPenetration asks the game for.
/// @note The folder is a guess (the exe keeps no __FILE__ string for it).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkpAabbPhantom;

    /// @note 8 bytes (vtable only). Skyrim implements it in bhkRagdollPenetrationUtil (castRay 0x140e9a500).
    class hkaRagdollRaycastInterface
    {
    public:
        /// @brief The class allocator: a fixed-size heap block (no recorded size).
        HK_FORCE_INLINE void* operator new(hkUlong nbytes) throw() { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }

        HK_FORCE_INLINE virtual ~hkaRagdollRaycastInterface() {}
        /// @brief Whether the segment `fromWS` → `toWS` hits anything near `phantomIn`'s AABB; the hit point and normal.
        /// @note The hkBool comes back through memory.
        virtual hkBool castRay(const hkpAabbPhantom* phantomIn, const hkVector4& fromWS, const hkVector4& toWS, hkVector4& hitPointWS,
                               hkVector4& hitNormalWS) = 0;   ///< slot 1
    };
} }
