/// @file
/// @brief hkPseudoRandomGenerator: Havok's linear congruential generator.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace havok
{
    /// @note Only what callers use so far. The multiplier/increment are the classic Numerical Recipes LCG (vanilla's
    ///       0x19660d / 0x3c6ef35f).
    class hkPseudoRandomGenerator
    {
    public:
        hkUint32 m_seed;      ///< +0x0
        hkUint32 m_current;   ///< +0x4

        /// @brief The class allocator. `throw()`: vanilla null-checks the block (hkbBehaviorGraph's constructors).
        HK_FORCE_INLINE void* operator new(hkUlong nbytes) throw() { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }

        HK_FORCE_INLINE hkPseudoRandomGenerator(int seed) : m_seed(seed), m_current(seed) {}

        HK_FORCE_INLINE hkUint32 getRand32()
        {
            m_current = 1664525U * m_current + 1013904223U;
            return m_current;
        }
        /// @brief A value in [0, range): the generator's high bits (the low ones of an LCG are weak), reduced.
        /// @note Name provisional (the shift by 13 is vanilla's; BSEventEveryNEventsModifier stores the result in an int8).
        HK_FORCE_INLINE hkUint32 getRandChar(int range) { return (getRand32() >> 13) % range; }
    };
} }
