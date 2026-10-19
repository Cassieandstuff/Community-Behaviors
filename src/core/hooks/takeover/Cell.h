#pragma once

#include <PCH.h>
#include <PluginLogger.h>

#include <core/debug/Settings.h>
#include <core/hooks/points/HookPoint.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

/// @file
/// @brief The cell table: one dispatch cell per reimplemented Havok function the takeover installed. Every route into
///        the vanilla function (vtable slot, direct call, another plugin) reaches its entry jump, which jumps through the
///        cell; the cell holds whatever runs it now — CB's implementation, or a chain of feature overrides ending in it.
/// @note The pristine implementation (schema/Havok, gated against vanilla) is never edited to change behavior: every
///       change is an override here, so what is original and what a feature changed stay apart.
/// @note Same idiom as HookPoint: subscribe `fn(Next next, args...)` with a priority; lower priority runs first and
///       outermost; call `next` to continue, or don't to replace.

namespace CB::core::hooks {

    /// @return the cell of the installed function whose symbol is `mangled`, or nullptr when it is not installed
    ///         (takeover off, class off, patched by another plugin, too short).
    /// @note Defined by the takeover (Takeover.cpp).
    [[nodiscard]] std::uintptr_t* FindCell(std::string_view mangled, std::string* why = nullptr);

    namespace detail {
        struct CellBase {
            virtual ~CellBase() = default;
            [[nodiscard]] virtual const char* Name() const noexcept = 0;
            virtual void                      Install()             = 0;
        };

        /// @note Function-local statics, not class statics: features subscribe at static init, which may run before a
        ///       namespace-scope or template-static vector would have been constructed (and then be wiped by it).
        inline std::vector<CellBase*>& WantedCells()
        {
            static std::vector<CellBase*> cells;
            return cells;
        }

        inline bool& CellsClosed()
        {
            static bool closed = false;
            return closed;
        }

        /// @brief Installs every cell that has a subscriber, then closes subscription.
        /// @pre The takeover has installed its functions (their cells exist), or is off (each cell then reports why).
        inline void InstallCells()
        {
            if (CellsClosed()) return;
            CellsClosed() = true;
            for (CellBase* c : WantedCells()) c->Install();
        }
    }  // namespace detail

    /// @brief CRTP base for one reimplemented function's cell. `Self` supplies `mangled` (its symbol, as the runtime image
    ///        records it), `name` (readable, for logs) and `id` (vanilla's Address Library id; 0 = none).
    /// @note A feature names one through Override<&fn> (<core/hooks/takeover/Override.h>), whose identity the registry
    ///       codegen generates from engine.yaml; a feature rarely writes its own.
    /// @note Members are free signatures with the object first (x64 has one calling convention, so `this` first is the
    ///       same call).
    template <class Self, class Sig>
    class Cell;

    template <class Self, class R, class... A>
    class Cell<Self, R(A...)>
    {
    public:
        /// @brief Continues the chain: the next subscriber, or the cell's implementation after the last one.
        struct Next {
            std::uint32_t at;
            R             operator()(A... a) const { return Cell::Dispatch(at, a...); }
        };

        using Fn = R (*)(Next next, A... a);

        /// @param flag a DebugFlags id; when non-empty and off at install, this subscriber is left out.
        /// @return false (and logs) once subscription has closed — the subscriber will never run.
        static bool Subscribe(Fn fn, Priority prio = Priority::Normal, const char* who = "?", std::string_view flag = {})
        {
            if (detail::CellsClosed()) {
                LOG_ERROR("havok cell: '{}' subscribed to {} after install — ignored", who, Self::name);
                return false;
            }
            auto& subs = Subs();
            if (subs.empty()) detail::WantedCells().push_back(&Point());
            subs.push_back({ fn, prio, who, flag });
            return true;
        }

        /// @return whether the chain is live (false: no subscriber left, or the function is not installed).
        [[nodiscard]] static bool Installed() noexcept { return s_impl != nullptr; }

        /// @brief Calls the function as any caller would: through vanilla's entry, so it reaches whatever runs it now
        ///        (CB's code and every override, or vanilla when the takeover left it alone).
        static R Call(A... a)
            requires(Self::id != 0)
        {
            static const auto entry = reinterpret_cast<R (*)(A...)>(REL::ID(Self::id).address());
            return entry(a...);
        }

    private:
        struct Sub {
            Fn               fn;
            Priority         prio;
            const char*      who;
            std::string_view flag;
        };

        struct PointT final : detail::CellBase {
            [[nodiscard]] const char* Name() const noexcept override { return Self::name; }
            void                      Install() override { Cell::Install(); }
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
            return s_impl(a...);
        }

        static void Install()
        {
            auto& subs = Subs();
            std::erase_if(subs, [](const Sub& s) {
                if (s.flag.empty() || debug::FlagOn(s.flag)) return false;
                LOG_INFO("havok cell: '{}' on {} is off by its flag {}", s.who, Self::name, s.flag);
                return true;
            });
            if (subs.empty()) return;
            std::stable_sort(subs.begin(), subs.end(), [](const Sub& l, const Sub& r) { return l.prio < r.prio; });
            std::string          why;
            std::uintptr_t* const cell = FindCell(Self::mangled, &why);
            if (!cell) {
                LOG_WARN("havok cell: {} — {}; its {} override(s) stay off", Self::name, why, subs.size());
                return;
            }
            s_chain = subs.data();
            s_count = static_cast<std::uint32_t>(subs.size());
            s_impl  = reinterpret_cast<R (*)(A...)>(*cell);
            *cell   = reinterpret_cast<std::uintptr_t>(&Detour);
            for (const Sub& s : subs) LOG_INFO("havok cell: {} overridden by '{}'", Self::name, s.who);
        }

        /// @invariant Set once at install, before the game runs the function; read lock-free on every call after.
        static inline const Sub*    s_chain = nullptr;
        static inline std::uint32_t s_count = 0;
        static inline R (*s_impl)(A...)     = nullptr;
    };

}  // namespace CB::core::hooks
