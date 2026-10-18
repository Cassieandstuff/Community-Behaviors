/// @file
/// @brief bhkCharacterPointCollector: the closest-points collector a character controller keeps for the bodies it
///        starts out overlapping.
/// @note Not matched here: declared only as far as the character controller's code uses it.
#pragma once

#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>

namespace CB { namespace schema
{
    /// @note 0x240 bytes; vtable 0x1419fb220.
    class bhkCharacterPointCollector : public hkpAllCdPointCollector
    {
    public:
        /// @note Out of line in vanilla (0x140f17650); `owner` (name provisional) is kept at +0x220.
        explicit bhkCharacterPointCollector(void* owner);
        /// @note Out of line in vanilla (0x140f176e0).
        virtual ~bhkCharacterPointCollector();
        /// @brief Clears the hits and the early-out distance (vanilla 0x140f17b00).
        /// @note Havok's own reset is the virtual at slot 2; the hkp header declares it non-virtual, so this one
        ///       introduces the slot.
        virtual void reset();   ///< slot 2

        /// @brief Dispatches the overlap starts and ends recorded since the last call to the bodies' listeners, then
        ///        drops the ended entries.
        /// @note Name provisional; out of line in vanilla (0x140f17b20).
        void FlushEvents();

        hkUint8 m_pad220[0x240 - 0x220];
    };
} }
