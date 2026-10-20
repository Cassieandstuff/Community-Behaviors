#pragma once

#include <PCH.h>
#include <PluginLogger.h>

#include <core/debug/Settings.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief The hook library's one subscriber chain. Override<&fn> (a declared function) and CallSite<symbol, Sig>
///        (one call instruction) are its two attachments; a feature subscribes the same way to either.
/// @note Subscribe `fn(Next next, args...)` with a priority: lower runs first and outermost, sees the call before
///       later subscribers and their result after them. Call `next` to continue (the next subscriber, then the
///       original), change its arguments, or don't call it to replace.
/// @note Lifecycle: subscribe during static initialization or SKSEPluginLoad → InstallAll() once → closed. The
///       chains never change after that, so dispatch takes no lock.

namespace CB::platform::hooks {

    enum class Priority : std::int32_t { First = -1000, Early = -100, Normal = 0, Late = 100, Last = 1000 };

    namespace detail {

        struct ChainBase {
            virtual ~ChainBase() = default;
            [[nodiscard]] virtual const char* Name() const noexcept = 0;
            virtual bool                      Install()             = 0;
        };

        /// @note Function-local statics: features subscribe at static initialization, which may run before a
        ///       namespace-scope or template-static vector would have been constructed (and then be wiped by it).
        inline std::vector<ChainBase*>& Wanted()
        {
            static std::vector<ChainBase*> chains;
            return chains;
        }

        inline bool& Closed()
        {
            static bool closed = false;
            return closed;
        }

    }  // namespace detail

    /// @brief Attaches every chain that has a subscriber, then closes subscription.
    /// @pre Called once, in SKSEPluginLoad, after the takeover installed its functions (so their cells exist),
    ///      after SKSE::AllocTrampoline (call sites), and after every subscriber registered.
    /// @note A chain that cannot attach (no pin on this runtime, not a CALL, MinHook refused) is logged and left
    ///       off: the game runs untouched there and its subscribers never fire.
    void InstallAll();

    /// @brief CRTP chain. `Self` supplies `name` (for logs) and
    ///        `static bool Attach(void* detour, void** original, std::string& why, const char*& how)`, which routes
    ///        the hooked code to `detour` and stores what it replaced in `*original` before the route goes live.
    /// @note Signatures are free functions with the object first (x64 has one calling convention).
    template <class Self, class Sig>
    class Chain;

    template <class Self, class R, class... A>
    class Chain<Self, R(A...)>
    {
    public:
        struct Next {
            std::uint32_t at;
            R             operator()(A... a) const { return Chain::Dispatch(at, a...); }
        };

        using Fn = R (*)(Next next, A... a);

        /// @param flag a DebugFlags id; when non-empty and off at install, this subscriber is left out.
        /// @return false (and logs) once subscription has closed: the subscriber will never run.
        static bool Subscribe(Fn fn, Priority prio = Priority::Normal, const char* who = "?", std::string_view flag = {})
        {
            if (detail::Closed()) {
                LOG_ERROR("hooks: '{}' subscribed to {} after InstallAll — ignored", who, Self::name);
                return false;
            }
            auto& subs = Subs();
            if (subs.empty()) detail::Wanted().push_back(&Point());
            subs.push_back({ fn, prio, who, flag });
            return true;
        }

        /// @return whether the chain is live (false: no subscriber left, or it could not attach).
        [[nodiscard]] static bool Installed() noexcept { return s_original != nullptr; }

    private:
        struct Sub {
            Fn               fn;
            Priority         prio;
            const char*      who;
            std::string_view flag;
        };

        struct PointT final : detail::ChainBase {
            [[nodiscard]] const char* Name() const noexcept override { return Self::name; }
            bool                      Install() override { return Chain::Install(); }
        };

        static std::vector<Sub>& Subs()
        {
            static std::vector<Sub> subs;
            return subs;
        }

        static PointT& Point()
        {
            static PointT p;
            return p;
        }

        static R Detour(A... a) { return Dispatch(0, a...); }

        static R Dispatch(std::uint32_t at, A... a)
        {
            if (at < s_count) return s_chain[at].fn(Next{ at + 1 }, a...);
            return s_original(a...);
        }

        static bool Install()
        {
            auto& subs = Subs();
            std::erase_if(subs, [](const Sub& s) {
                if (s.flag.empty() || core::debug::FlagOn(s.flag)) return false;
                LOG_INFO("hooks: '{}' on {} is off by its flag {}", s.who, Self::name, s.flag);
                return true;
            });
            if (subs.empty()) return false;
            std::stable_sort(subs.begin(), subs.end(), [](const Sub& l, const Sub& r) { return l.prio < r.prio; });
            s_chain = subs.data();
            s_count = static_cast<std::uint32_t>(subs.size());
            std::string why;
            const char* how      = "?";
            void*       original = nullptr;
            if (!Self::Attach(reinterpret_cast<void*>(&Detour), &original, why, how) || !original) {
                s_count = 0;
                LOG_WARN("hooks: {} not attached — {}; its {} subscriber(s) stay off", Self::name, why, subs.size());
                return false;
            }
            s_original = reinterpret_cast<R (*)(A...)>(original);
            for (const Sub& s : subs) LOG_INFO("hooks: {} subscribed by '{}' (through {})", Self::name, s.who, how);
            return true;
        }

        /// @invariant Set once at install, before the game runs the hooked code; read lock-free on every call after.
        static inline const Sub*    s_chain    = nullptr;
        static inline std::uint32_t s_count    = 0;
        static inline R (*s_original)(A...)    = nullptr;
    };

    namespace detail {

        /// @brief An entry detour (MinHook) at `at`; `*original` gets the trampoline before the hook is enabled.
        bool AttachEntry(std::uintptr_t at, void* detour, void** original, std::string& why);
        /// @brief A call detour (the SKSE trampoline's write_call<5>) on the CALL rel32 at `at`.
        bool AttachCall(std::uintptr_t at, void* detour, void** original, std::string& why);

    }  // namespace detail

}  // namespace CB::platform::hooks
