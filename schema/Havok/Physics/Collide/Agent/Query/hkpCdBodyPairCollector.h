/// @file
/// @brief hkpCdBodyPairCollector: the collector a penetration query reports overlapping body pairs to.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpCdBody;

    /// @note Only what the inline constructors use is defined; the interface is declared as the vtable needs it
    ///       (slots per the Havok SDK, unverified).
    class hkpCdBodyPairCollector
    {
    public:
        /// @brief Set by a collector that wants no more pairs.
        hkBool m_earlyOut;   ///< +0x08

        HK_FORCE_INLINE hkpCdBodyPairCollector() { reset(); }
        virtual ~hkpCdBodyPairCollector() {}
        virtual void addCdBodyPair(const hkpCdBody& bodyA, const hkpCdBody& bodyB) = 0;
        HK_FORCE_INLINE void reset() { m_earlyOut = false; }
    };
} }
