#include "PCH.h"

#include "features/Animation 64/Watch.h"

#include "core/debug/Settings.h"

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <PluginLogger.h>

#include <TlHelp32.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <thread>

namespace CB::feature::widen {

    namespace {

        namespace hk = CB::havok;

        constexpr int         kSlots      = 4;
        constexpr std::size_t kCandidates = 64;
        constexpr std::size_t kHits       = 1024;
        constexpr std::size_t kField      = offsetof(hk::hkbClipGenerator, m_animationBindingIndex);

        /// @brief One instruction seen touching a watched field.
        struct Hit {
            std::atomic<std::uintptr_t> rip{ 0 };
            std::atomic<std::uint32_t>  count{ 0 };
            bool                        logged = false;   ///< probe thread only
        };

        struct State {
            std::array<std::atomic<std::uintptr_t>, kSlots>      armed{};        ///< field addresses in the debug registers
            std::array<std::atomic<std::uintptr_t>, kCandidates> candidates{};   ///< clips offered, a ring
            std::atomic<std::uint32_t>                           next{ 0 };
            std::array<Hit, kHits>                               hits;
            std::atomic<std::uint64_t>                           lost{ 0 };      ///< hits the table had no room for
            std::atomic<std::uint64_t>                           stale{ 0 };     ///< hits on memory no longer a CB clip
            std::atomic<std::uintptr_t>                          vtable{ 0 };    ///< CB's clip vtable (an offered clip's)
            DWORD                                                probe = 0;
        };
        State& W()
        {
            static State s;
            return s;
        }

        /// @note Runs on the faulting thread inside the exception dispatcher: no allocation, no lock, no logging.
        void Record(std::uintptr_t rip)
        {
            State& w = W();
            for (std::size_t i = 0, h = (rip >> 2) % kHits; i < 16; ++i, h = (h + 1) % kHits) {
                std::uintptr_t expected = 0;
                if (w.hits[h].rip.compare_exchange_strong(expected, rip) || expected == rip) {
                    w.hits[h].count.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
            w.lost.fetch_add(1, std::memory_order_relaxed);
        }

        /// @return `*at`, or 0 when it cannot be read (an offered clip's memory may since have been freed).
        std::uintptr_t ReadPointer(std::uintptr_t at)
        {
            __try {
                return *reinterpret_cast<const volatile std::uintptr_t*>(at);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }

        /// @note Every data watchpoint is the watch's: it writes every thread's debug registers. A thread re-armed late
        ///       still holds the previous round's addresses (clip fields too), so a hit is never matched against the
        ///       current round; passing one on crashes the game (nothing else handles it: 2026-10-22, a clip copy's
        ///       memcpy during cloneNode).
        LONG CALLBACK Handler(EXCEPTION_POINTERS* e)
        {
            if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
            CONTEXT* ctx = e->ContextRecord;
            const DWORD64 fired = ctx->Dr6 & 0xF;
            if (fired == 0) return EXCEPTION_CONTINUE_SEARCH;
            /// @note A hit counts only while the watched field still belongs to a clip carrying CB's vtable: a clip
            ///       freed while watched leaves its memory to whatever comes next (the first run logged physics
            ///       broadphase and actor code there).
            const DWORD64        dr[kSlots] = { ctx->Dr0, ctx->Dr1, ctx->Dr2, ctx->Dr3 };
            const std::uintptr_t vtable     = W().vtable.load(std::memory_order_relaxed);
            bool                 live       = false;
            for (int i = 0; i < kSlots; ++i)
                if (((fired >> i) & 1) && dr[i] > kField) live = live || ReadPointer(static_cast<std::uintptr_t>(dr[i]) - kField) == vtable;
            if (live) Record(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress));
            else W().stale.fetch_add(1, std::memory_order_relaxed);
            ctx->Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        /// @brief Puts `fields` in every other thread's debug registers: read/write watchpoints, 2 bytes each.
        /// @note  Each thread is suspended only around its own Get/SetThreadContext: nothing allocates while one is held.
        void ArmAllThreads(const std::array<std::uintptr_t, kSlots>& fields)
        {
            DWORD64 dr7 = 0;
            for (int i = 0; i < kSlots; ++i)
                if (fields[i]) dr7 |= (1ull << (2 * i)) | (3ull << (16 + 4 * i)) | (1ull << (18 + 4 * i));
            const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap == INVALID_HANDLE_VALUE) return;
            THREADENTRY32 te{ sizeof(te) };
            const DWORD   pid = GetCurrentProcessId(), self = GetCurrentThreadId();
            for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te)) {
                if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
                const HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
                if (!t) continue;
                if (SuspendThread(t) != DWORD(-1)) {
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                    if (GetThreadContext(t, &ctx)) {
                        ctx.Dr0 = fields[0], ctx.Dr1 = fields[1], ctx.Dr2 = fields[2], ctx.Dr3 = fields[3];
                        ctx.Dr7 = dr7;
                        SetThreadContext(t, &ctx);
                    }
                    ResumeThread(t);
                }
                CloseHandle(t);
            }
            CloseHandle(snap);
        }

        /// @return where `rip` is: "exe 0x<rva> (0x14... in Ghidra)", "plugin", "no module (the takeover image)", or a
        ///         DLL's file name.
        std::string Where(std::uintptr_t rip, bool& plugin)
        {
            plugin = false;
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(rip), &module))
                return "no module (the takeover image)";
            const std::uintptr_t exe = REL::Module::get().base();
            if (reinterpret_cast<std::uintptr_t>(module) == exe) {
                const std::uintptr_t rva = rip - exe;
                return std::format("exe +{:#x} ({:#x} in Ghidra)", rva, 0x140000000ull + rva);
            }
            HMODULE ours = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&Where), &ours);
            if (module == ours) {
                plugin = true;
                return "plugin";
            }
            char path[MAX_PATH]{};
            GetModuleFileNameA(module, path, MAX_PATH);
            const std::string p(path);
            const auto        slash = p.find_last_of("\\/");
            return slash == std::string::npos ? p : p.substr(slash + 1);
        }

        /// @brief Logs every instruction seen for the first time; returns how many hits came from the plugin.
        std::uint64_t Flush()
        {
            std::uint64_t plugin = 0;
            for (Hit& h : W().hits) {
                const std::uintptr_t rip = h.rip.load(std::memory_order_relaxed);
                if (!rip) continue;
                bool        fromPlugin = false;
                std::string where      = Where(rip, fromPlugin);
                if (fromPlugin) {
                    plugin += h.count.load(std::memory_order_relaxed);
                    continue;
                }
                if (h.logged) continue;
                h.logged = true;
                LOG_INFO("animation 64 watch: the 16-bit binding index touched by the instruction before {:#x}, {}", rip, where);
            }
            return plugin;
        }

        /// @brief The newest offered clips still carrying CB's vtable, at most one field per slot.
        std::array<std::uintptr_t, kSlots> Pick()
        {
            State&                              w      = W();
            std::array<std::uintptr_t, kSlots>  fields{};
            const std::uintptr_t                vtable = w.vtable.load(std::memory_order_relaxed);
            const std::uint32_t                 next   = w.next.load(std::memory_order_relaxed);
            int                                 n      = 0;
            for (std::uint32_t k = 1; k <= kCandidates && n < kSlots; ++k) {
                const std::uintptr_t clip = w.candidates[(next - k) % kCandidates].load(std::memory_order_relaxed);
                if (!clip || ReadPointer(clip) != vtable) continue;
                const std::uintptr_t field = clip + kField;
                bool                 seen  = false;
                for (int i = 0; i < n; ++i) seen = seen || fields[i] == field;
                if (!seen) fields[n++] = field;
            }
            return fields;
        }

        void Probe()
        {
            W().probe = GetCurrentThreadId();
            LOG_INFO("animation 64 watch: on - watching the 16-bit binding index of the newest activated clips");
            for (std::uint64_t tick = 1;; ++tick) {
                Sleep(500);
                const auto fields = Pick();
                for (int i = 0; i < kSlots; ++i) W().armed[i].store(fields[i], std::memory_order_relaxed);
                ArmAllThreads(fields);
                const std::uint64_t plugin = Flush();
                if (tick % 60 == 0)
                    LOG_INFO("animation 64 watch: {} access(es) from converted code (the plugin) so far; {} on memory no longer a "
                             "clip (skipped); {} lost to a full table",
                             plugin, W().stale.load(std::memory_order_relaxed), W().lost.load(std::memory_order_relaxed));
            }
        }

        bool Enabled()
        {
            static const bool on = core::debug::FlagOn("animation64_watch");
            return on;
        }

    }  // namespace

    void OfferToWatch(const void* clip)
    {
        if (!Enabled()) return;
        State& w = W();
        w.vtable.store(*static_cast<const std::uintptr_t*>(clip), std::memory_order_relaxed);
        w.candidates[w.next.fetch_add(1, std::memory_order_relaxed) % kCandidates].store(reinterpret_cast<std::uintptr_t>(clip),
                                                                                          std::memory_order_relaxed);
        static std::once_flag started;
        std::call_once(started, [] {
            AddVectoredExceptionHandler(1, &Handler);
            std::thread(&Probe).detach();
        });
    }

}  // namespace CB::feature::widen
