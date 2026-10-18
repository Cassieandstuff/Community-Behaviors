/// @file
/// @brief BSSpinLock: Bethesda's reentrant spin lock (a thread spins, then sleeps, until it owns it), and its scope guard.
/// @note Not Havok: declared as far as the proxy character controller's code inlines it.
#pragma once

#include <Common/Base/hkBase.h>

extern "C" __declspec(dllimport) DWORD __stdcall GetCurrentThreadId(void);
extern "C" __declspec(dllimport) void __stdcall Sleep(DWORD dwMilliseconds);
extern "C" long _InterlockedCompareExchange(long volatile* destination, long exchange, long comparand);
extern "C" long _InterlockedIncrement(long volatile* addend);
extern "C" long _InterlockedDecrement(long volatile* addend);
#pragma intrinsic(_InterlockedCompareExchange, _InterlockedIncrement, _InterlockedDecrement)

namespace CB { namespace schema
{
    /// @note 8 bytes.
    class BSSpinLock
    {
    public:
        /// @brief Takes the lock unless another thread holds it.
        HK_FORCE_INLINE bool TryLock() { return _InterlockedCompareExchange(&m_lockCount, 1, 0) == 0; }

        /// @brief Takes the lock (again, if this thread holds it): one pause, then sleeps of 0 ms, and of 1 ms from the
        ///        10000th try on.
        HK_FORCE_INLINE void Lock()
        {
            const DWORD thread = GetCurrentThreadId();
            _mm_lfence();
            if (m_owningThread == thread) {
                _InterlockedIncrement(&m_lockCount);
                return;
            }
            if (!TryLock()) {
                _mm_pause();
                if (!TryLock()) {
                    hkUint32 spins = 0;
                    do {
                        DWORD milliseconds = 1;
                        if (spins < 10000) {
                            ++spins;
                            milliseconds = 0;
                        }
                        Sleep(milliseconds);
                    } while (!TryLock());
                }
                _mm_lfence();
            }
            m_owningThread = thread;
            _mm_sfence();
        }

        /// @brief Drops one hold, if this thread holds the lock.
        HK_FORCE_INLINE void Unlock()
        {
            _mm_lfence();
            const DWORD thread = GetCurrentThreadId();
            if (m_owningThread == thread) {
                if (m_lockCount == 1) {
                    m_owningThread = 0;
                    _mm_mfence();
                    _InterlockedCompareExchange(&m_lockCount, 0, 1);
                } else {
                    _InterlockedDecrement(&m_lockCount);
                }
            }
        }

        DWORD m_owningThread;   ///< +0x00
        /// @brief How many holds the owning thread has.
        long  m_lockCount;      ///< +0x04
    };

    /// @brief Holds `lock` for the guard's lifetime.
    class BSSpinLockGuard
    {
    public:
        HK_FORCE_INLINE explicit BSSpinLockGuard(BSSpinLock& lock) : m_lock(lock) { m_lock.Lock(); }
        HK_FORCE_INLINE ~BSSpinLockGuard() { m_lock.Unlock(); }

        BSSpinLock& m_lock;
    };
} }
