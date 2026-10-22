/// @file
/// @brief The per-thread profiling stream and the HK_TIMER_BEGIN/END markers.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace havok
{
    extern DWORD hkMonitorStream_s_tls;   ///< the stream's TLS index

    struct hkMonitorStream
    {
        struct TimerCommand
        {
            const char* m_commandAndMonitor;
            hkUint32    m_time0;
            hkUint32    m_pad;
        };
        /// @brief A list-begin command: the list's name, then the first split's.
        struct TimerBeginListCommand : TimerCommand
        {
            const char* m_nameOfFirstSplit;
        };

        char* m_start;
        char* m_end;
        char* m_capacity;
        char* m_capacityMinus16;

        static HK_FORCE_INLINE hkMonitorStream& getInstance() { return *static_cast<hkMonitorStream*>(TlsGetValue(hkMonitorStream_s_tls)); }
        HK_FORCE_INLINE bool memoryAvailable() const { return m_end < m_capacityMinus16; }
    };
} }

/// @brief Writes one timer command (`Tt<name>` begin / `Et` end) with the low 32 bits of the TSC.
#define HK_TIMER_COMMAND(CMD)                                                                                       \
    {                                                                                                               \
        ::CB::havok::hkMonitorStream& mStream = ::CB::havok::hkMonitorStream::getInstance();                      \
        if (mStream.memoryAvailable()) {                                                                            \
            ::CB::havok::hkMonitorStream::TimerCommand* h = reinterpret_cast<::CB::havok::hkMonitorStream::TimerCommand*>(mStream.m_end); \
            h->m_commandAndMonitor = CMD;                                                                           \
            h->m_time0             = static_cast<::CB::havok::hkUint32>(__rdtsc());                               \
            mStream.m_end          = reinterpret_cast<char*>(h + 1);                                               \
        }                                                                                                           \
    }
#define HK_TIMER_BEGIN(NAME) HK_TIMER_COMMAND("Tt" NAME)
#define HK_TIMER_END()       HK_TIMER_COMMAND("Et")

/// @brief Opens a timer list (`Lt<name>`) whose first split is `St<first>`; HK_TIMER_SPLIT_LIST starts the next split,
///        HK_TIMER_END_LIST (`lt`) closes it.
#define HK_TIMER_BEGIN_LIST(NAME, FIRST)                                                                            \
    {                                                                                                               \
        ::CB::havok::hkMonitorStream& mStream = ::CB::havok::hkMonitorStream::getInstance();                      \
        if (mStream.memoryAvailable()) {                                                                            \
            ::CB::havok::hkMonitorStream::TimerBeginListCommand* h = reinterpret_cast<::CB::havok::hkMonitorStream::TimerBeginListCommand*>(mStream.m_end); \
            h->m_commandAndMonitor = "Lt" NAME;                                                                     \
            h->m_nameOfFirstSplit  = "St" FIRST;                                                                    \
            h->m_time0             = static_cast<::CB::havok::hkUint32>(__rdtsc());                               \
            mStream.m_end          = reinterpret_cast<char*>(h + 1);                                               \
        }                                                                                                           \
    }
#define HK_TIMER_SPLIT_LIST(NAME) HK_TIMER_COMMAND("St" NAME)
#define HK_TIMER_END_LIST()       HK_TIMER_COMMAND("lt")
