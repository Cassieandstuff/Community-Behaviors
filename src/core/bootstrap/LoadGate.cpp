#include "PCH.h"

#include "core/bootstrap/LoadGate.h"
#include "core/bootstrap/CompileGate.h"

#include <platform/game/SaveLoad.h>
#include <platform/hooks/Override.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace CB::core::bootstrap {

    namespace {

        namespace game  = CB::platform::game;
        namespace hooks = CB::platform::hooks;

        using ProcessNext = hooks::Override<&game::BGSSaveLoadManager_ProcessEvents>::Next;
        using LoadNext    = hooks::Override<&game::BGSSaveLoadManager_Load>::Next;
        using NewGameNext = hooks::Override<&game::MainMenu_StartNewGame>::Next;

        using Clock = std::chrono::steady_clock;

        double SecondsSince(Clock::rep t0) { return std::chrono::duration<double>(Clock::now().time_since_epoch() - Clock::duration(t0)).count(); }

        std::atomic<bool>       s_holding{ false };
        /// @brief When a load was asked for while the warm-up ran (0 = none), for the release's log line.
        std::atomic<Clock::rep> s_loadAskedAt{ 0 };
        std::atomic<bool>       s_newGamePending{ false };

        /// @note EnsureCompiledAndArmed kicks the warm-up if no open has yet: a hold can then never outlive a warm-up
        ///       that was never started.
        void OnProcessEvents(ProcessNext next, RE::BGSSaveLoadManager* manager)
        {
            if (!CB::WarmUpFinished()) {
                CB::EnsureCompiledAndArmed();
                if (!s_holding.exchange(true, std::memory_order_relaxed))
                    LOG_INFO("load gate: holding the save/load manager's requests (game loads) until the warm-up finishes.");
                return;
            }
            if (s_holding.exchange(false, std::memory_order_relaxed)) {
                const Clock::rep asked = s_loadAskedAt.exchange(0, std::memory_order_relaxed);
                if (asked != 0)
                    LOG_INFO("load gate: warm-up finished - the game load asked for {:.1f} s ago starts now.", SecondsSince(asked));
                else
                    LOG_INFO("load gate: warm-up finished - save/load requests run again (none was waiting).");
            }
            next(manager);
        }

        bool OnLoad(LoadNext next, RE::BGSSaveLoadManager* manager, const char* fileName, std::int32_t deviceID,
                    std::uint32_t outputStats, bool checkForMods)
        {
            if (!CB::WarmUpFinished()) {
                s_loadAskedAt.store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
                LOG_INFO("load gate: game load '{}' asked for on thread {} while the warm-up runs - it starts when the "
                         "warm-up finishes.", fileName ? fileName : "", ::GetCurrentThreadId());
            }
            return next(manager, fileName, deviceID, outputStats, checkForMods);
        }

        /// @note One deferred start: a second click while it waits is dropped. The replay runs on the main thread,
        ///       like the click; the callback reads nothing from its args (SaveLoad.h), so they carry only the
        ///       original handler and movie.
        void OnStartNewGame(NewGameNext next, const RE::FxDelegateArgs& args)
        {
            if (CB::WarmUpFinished()) return next(args);
            if (s_newGamePending.exchange(true)) return;
            LOG_INFO("load gate: New Game waits for the warm-up to finish.");
            RE::FxDelegateHandler* const handler = args.GetHandler();
            RE::GFxMovieView* const      movie   = args.GetMovie();
            const Clock::rep             t0      = Clock::now().time_since_epoch().count();
            std::thread([next, handler, movie, t0] {
                CB::EnsureCompiledAndArmed();
                CB::WaitForCompile();
                auto* tasks = SKSE::GetTaskInterface();
                if (!tasks) {
                    LOG_ERROR("load gate: no SKSE task interface - the held New Game cannot be replayed.");
                    s_newGamePending.store(false);
                    return;
                }
                tasks->AddTask([next, handler, movie, t0] {
                    LOG_INFO("load gate: warm-up finished - New Game starts after {:.1f} s.", SecondsSince(t0));
                    const RE::FxDelegateArgs later(RE::GFxValue{}, handler, movie, nullptr, 0);
                    next(later);
                    s_newGamePending.store(false);
                });
            }).detach();
        }

    }  // namespace

    void InstallLoadGate()
    {
        hooks::Override<&game::BGSSaveLoadManager_ProcessEvents>::Subscribe(&OnProcessEvents, hooks::Priority::First, "LoadGate");
        hooks::Override<&game::BGSSaveLoadManager_Load>::Subscribe(&OnLoad, hooks::Priority::First, "LoadGate");
        hooks::Override<&game::MainMenu_StartNewGame>::Subscribe(&OnStartNewGame, hooks::Priority::First, "LoadGate");
    }

}  // namespace CB::core::bootstrap
