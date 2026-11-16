#include "PCH.h"

#include "CharMovementModel.h"

#include <core/debug/Settings.h>

#include <algorithm>
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

        struct PublishedAhead {
            FacingAhead ahead;
            double      at = -1.0e9;
        };
        /// @brief Every predicted actor's facing ahead, by FormID. @note Never pruned, as g_published.
        struct AheadTable {
            std::mutex                                     lock;
            std::unordered_map<RE::FormID, PublishedAhead> byActor;
        };
        AheadTable g_ahead;

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

    void PublishFacingAhead(RE::FormID actor, const FacingAhead& ahead)
    {
        std::scoped_lock lock(g_ahead.lock);
        g_ahead.byActor[actor] = PublishedAhead{ ahead, Now() };
    }

    bool FacingAt(RE::FormID actor, float seconds, float& yaw)
    {
        FacingAhead a;
        {
            std::scoped_lock lock(g_ahead.lock);
            const auto       it = g_ahead.byActor.find(actor);
            if (it == g_ahead.byActor.end() || Now() - it->second.at > kStale) return false;
            a = it->second.ahead;
        }
        if (a.step <= 0.0f) return false;
        const float f = std::clamp(seconds / a.step, 0.0f, static_cast<float>(FacingAhead::kPoints));
        const int   i = (std::min)(static_cast<int>(f), FacingAhead::kPoints - 1);
        yaw           = a.yaw[i] + (a.yaw[i + 1] - a.yaw[i]) * (f - static_cast<float>(i));
        return true;
    }

    bool On()
    {
        static const bool on = core::debug::FlagOn("char_movement_model");
        return on;
    }

}  // namespace CB::feature::controller::move
