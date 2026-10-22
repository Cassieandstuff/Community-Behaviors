/// @file
/// @brief hkCriticalSection: a Win32 critical section.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x28 bytes (the CRITICAL_SECTION); only the destructor is reimplemented so far.
    struct hkCriticalSection
    {
        hkUint8 m_section[0x28];

        HK_FORCE_INLINE ~hkCriticalSection() { DeleteCriticalSection(reinterpret_cast<_RTL_CRITICAL_SECTION*>(m_section)); }
    };
} }
