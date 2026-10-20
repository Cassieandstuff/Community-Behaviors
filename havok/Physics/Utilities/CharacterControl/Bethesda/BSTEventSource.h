/// @file
/// @brief BSTEventSource: Bethesda's typed event dispatcher (sinks register; SendEvent notifies each).
/// @note Not Havok: declared only as far as the character controller's code uses it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x58 bytes: the sink array, the pending register / unregister arrays, a lock and a notifying flag
    ///       (padding here).
    template <class Event> class BSTEventSource
    {
    public:
        /// @note Out of line in vanilla (one instance per event type).
        ~BSTEventSource();

        /// @brief Notifies every registered sink of `event`.
        /// @note Out of line in vanilla (one instance per event type).
        void SendEvent(const Event* event);

        hkUint8 m_pad00[0x58];
    };

    /// @brief A Gamebryo point / vector in game units.
    /// @note 0xc bytes.
    struct NiPoint3
    {
        float x;   ///< +0x00
        float y;   ///< +0x04
        float z;   ///< +0x08

        HK_FORCE_INLINE NiPoint3() {}
        /// @note Its arguments evaluate right to left (z first), as vanilla's do.
        HK_FORCE_INLINE NiPoint3(float px, float py, float pz) : x(px), y(py), z(pz) {}
        /// @note User-defined, as Gamebryo's: a copy then moves float by float (movss), not as a block (movsd + mov).
        HK_FORCE_INLINE NiPoint3& operator=(const NiPoint3& p)
        {
            x = p.x;
            y = p.y;
            z = p.z;
            return *this;
        }

        /// @brief (0, 0, 0).
        static const NiPoint3 ZERO;
    };
} }
