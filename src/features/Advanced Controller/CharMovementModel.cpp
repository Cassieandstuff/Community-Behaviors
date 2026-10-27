#include "PCH.h"

#include "CharMovementModel.h"

#include <core/debug/Settings.h>

#include <chrono>
#include <mutex>
#include <unordered_map>

/// @file
/// @brief The movement model's game half: each driven actor's latest snapshot, by FormID, and the flag. The step is
///        the engine's (locomotion/Movement.cpp).

namespace CB::feature::controller::move {

    namespace {

        struct Published {
            Snapshot snap;
            double   at = -1.0e9;
        };
        /// @brief Every driven actor's latest snapshot, by FormID.
        /// @note Never pruned: only the actors a frame driver holds publish (the player, a ridden horse), a handful.
        struct PublishedTable {
            std::mutex                                 lock;
            std::unordered_map<RE::FormID, Published> byActor;
        };
        PublishedTable   g_published;
        constexpr double kStale = 0.5;

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    }  // namespace

    bool Current(RE::FormID actor, Snapshot& out)
    {
        std::scoped_lock lock(g_published.lock);
        const auto       it = g_published.byActor.find(actor);
        if (it == g_published.byActor.end() || Now() - it->second.at > kStale) return false;
        out = it->second.snap;
        return true;
    }

    bool Current(Snapshot& out) { return Current(kPlayerId, out); }

    void Publish(RE::FormID actor, const Snapshot& snap)
    {
        std::scoped_lock lock(g_published.lock);
        g_published.byActor[actor] = Published{ snap, Now() };
    }

    bool On()
    {
        static const bool on = core::debug::FlagOn("char_movement_model");
        return on;
    }

}  // namespace CB::feature::controller::move
