#pragma once

#include <PCH.h>
#include <PluginLogger.h>

#include <platform/hooks/factory/Install.h>
#include <platform/hooks/registry/Registry.h>

#include <MinHook.h>

#include <algorithm>
#include <cstdint>
#include <vector>

/// @file
/// @brief The shared hook interface for everything inside CB: the core owns each engine seam, installs ONE
///        detour there (only if something subscribed), and runs an ordered chain of subscribers through it.
/// @note Lifecycle: subscribe during SKSEPluginLoad → InstallAll() once → subscription is closed. The
///       subscriber lists are never mutated after that, so dispatch takes no lock (some seams — clip activate —
///       run for every clip on every actor).

namespace CB::platform::hooks {

/// @brief How a hook point attaches to the engine.
/// @note Entry = MinHook on a function entry (every caller); Call = write_call on ONE call instruction (that
///       caller only).
enum class Seam : std::uint8_t { Entry, Call };

/// @brief Subscriber order. Lower runs first and is OUTERMOST: it sees the call before later subscribers and
///        their result after them.
enum class Priority : std::int32_t { First = -1000, Early = -100, Normal = 0, Late = 100, Last = 1000 };

namespace detail {
    struct PointBase {
        virtual ~PointBase() = default;
        [[nodiscard]] virtual const char* Name() const noexcept = 0;
        [[nodiscard]] virtual bool Install() = 0;
    };

    /// @brief Hook points that have at least one subscriber, in first-subscription order.
    inline std::vector<PointBase*>& Wanted()
    {
        static std::vector<PointBase*> points;
        return points;
    }

    inline bool& Closed()
    {
        static bool closed = false;
        return closed;
    }
}  // namespace detail

template <class Self, class Sig>
class HookPoint;

/// @brief CRTP base for one engine seam. `Self` supplies `name`, `seam`, and `symbol` (an engine
///        registry Symbol).
/// @note Member functions are declared as free signatures with the object as the first parameter (x64 has a
///       single calling convention, so a `this` call and a free call with `this` first are identical).
template <class Self, class R, class... A>
class HookPoint<Self, R(A...)>
{
public:
    /// @brief Continues the chain: the next subscriber, or the engine's original after the last one.
    struct Next {
        std::uint32_t at;
        R operator()(A... a) const { return HookPoint::Dispatch(at, a...); }
    };

    /// @brief A subscriber: observe (call `next` unchanged), adjust arguments, or replace (don't call `next`).
    using Fn = R (*)(Next next, A... a);

    /// @return false (and logs) once subscription has closed — the subscriber will never run.
    static bool Subscribe(Fn fn, Priority prio = Priority::Normal, const char* who = "?")
    {
        if (detail::Closed()) {
            LOG_ERROR("hooks: '{}' subscribed to {} after InstallAll — ignored", who, Self::name);
            return false;
        }
        if (s_subs.empty()) {
            detail::Wanted().push_back(&s_point);
        }
        s_subs.push_back({ fn, prio, who });
        return true;
    }

    /// @return whether this seam's detour is live (false: no subscribers, unavailable on this runtime, or the
    ///         install failed — in every case the engine runs untouched and no subscriber runs).
    [[nodiscard]] static bool Installed() noexcept { return s_original != nullptr; }

private:
    struct Sub {
        Fn          fn;
        Priority    prio;
        const char* who;
    };

    struct Point final : detail::PointBase {
        [[nodiscard]] const char* Name() const noexcept override { return Self::name; }
        [[nodiscard]] bool        Install() override { return HookPoint::Install(); }
    };

    static R Detour(A... a) { return Dispatch(0, a...); }

    static R Dispatch(std::uint32_t at, A... a)
    {
        if (at < s_subs.size()) {
            return s_subs[at].fn(Next{ at + 1 }, a...);
        }
        return s_original(a...);
    }

    static bool Install()
    {
        std::stable_sort(s_subs.begin(), s_subs.end(),
                         [](const Sub& l, const Sub& r) { return l.prio < r.prio; });
        using Original = R (*)(A...);

        if (!engine::Available(Self::symbol)) {
            LOG_WARN("hooks: {} — {} has no verified pin on this runtime", Self::name, Self::symbol.name);
            return false;
        }
        const std::uintptr_t at = engine::Resolve(Self::symbol);
        if constexpr (Self::seam == Seam::Call) {
            static_assert(Self::symbol.kind == engine::Kind::Call, "a Call seam needs a `call` registry symbol");
            s_original = reinterpret_cast<Original>(InstallCallDetour<5>(at, &Detour, Self::name));
        } else {
            static_assert(Self::symbol.kind == engine::Kind::Func, "an Entry seam needs a `func` registry symbol");
            void* original = nullptr;
            if (MH_CreateHook(reinterpret_cast<void*>(at), reinterpret_cast<void*>(&Detour), &original) != MH_OK ||
                MH_EnableHook(reinterpret_cast<void*>(at)) != MH_OK) {
                LOG_WARN("hooks: {} — MinHook failed at 0x{:X}", Self::name, at);
                return false;
            }
            s_original = reinterpret_cast<Original>(original);
            LOG_INFO("hook: entry detour installed at 0x{:X} ({})", at, Self::name);
        }
        return s_original != nullptr;
    }

    static inline std::vector<Sub> s_subs;
    static inline R (*s_original)(A...) = nullptr;
    static inline Point s_point;
};

/// @brief Install every hook point that has a subscriber, then close subscription. Call ONCE, in
///        SKSEPluginLoad, after SKSE::AllocTrampoline and after every subscriber has registered.
/// @note A point that fails (or is unavailable on this runtime) is logged and left uninstalled — the engine runs
///       untouched there and its subscribers simply never fire; nothing else is affected.
inline void InstallAll()
{
    if (detail::Closed()) {
        return;
    }
    detail::Closed() = true;

    const MH_STATUS mh = MH_Initialize();
    if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_WARN("hooks: MH_Initialize failed ({}) — Entry seams will not install", static_cast<int>(mh));
    }

    std::size_t live = 0;
    for (detail::PointBase* p : detail::Wanted()) {
        if (p->Install()) {
            ++live;
        } else {
            LOG_WARN("hooks: {} not installed — its subscribers stay off", p->Name());
        }
    }
    LOG_INFO("hooks: {}/{} subscribed hook points installed", live, detail::Wanted().size());
}

}  // namespace CB::platform::hooks
