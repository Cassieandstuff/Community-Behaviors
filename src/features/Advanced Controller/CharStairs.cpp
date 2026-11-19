#include "PCH.h"

#include "CharStairs.h"

#include "CharSurface.h"

#include <core/debug/Settings.h>
#include <hky/locomotion/Stairs.h>
#include <hky/relay/Relay.h>
#include <hky/relay/Surface.h>
#include <platform/physics/GameWorld.h>
#include <platform/profile/Profile.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CB::feature::controller::stairs {

    namespace {

        namespace lc  = hky::locomotion;
        namespace ph  = hky::physics;
        platform::physics::QueryTag kMarchTag{ "char.stairs" };
        using Clock   = std::chrono::steady_clock;

        constexpr float kGameUnitsPerHavok = ph::kHavokToGame;

        /// @brief The survey's log lines, so a long session on stairs cannot flood the log.
        constexpr int kLogBudget = 600;

        std::atomic<bool> g_rulesAsk{ false };
        std::atomic<int>  g_logged{ 0 };

        bool Survey()
        {
            static const bool on = core::debug::FlagOn("char_stairs_survey");
            return on;
        }

        /// @brief The thresholds, with every catalog material whose name holds "Stairs" as the hint.
        /// @note  By the catalog's names (StoneStairs, WoodAsStairs, StoneStairsBroken...): the game's own stairs test
        ///        lists the same family. Rise and tread stay the spec's defaults until the survey measures them.
        const lc::StairsTunables& Tunables()
        {
            static const lc::StairsTunables t = [] {
                lc::StairsTunables out;
                for (const hky::relay::Choice& c : hky::relay::Choices("Material"))
                    if (c.name.find("Stairs") != std::string_view::npos) out.stairMaterials.push_back({ static_cast<std::uint32_t>(c.value) });
                return out;
            }();
            return t;
        }

        struct Entry {
            lc::StairsDetector detector;
            Clock::time_point  last{};
            bool               flight = false;   ///< the survey logged the flight held now
        };

        /// @brief Each character's detector. Only the thread stepping a character touches its Entry; the lock covers
        ///        finding it.
        struct Table {
            std::mutex                                                lock;
            std::unordered_map<RE::FormID, std::unique_ptr<Entry>>    byActor;
        };
        Table& T()
        {
            static Table t;
            return t;
        }

        /// @param dt the seconds since `actor`'s last step, at most 0.25 (0 on its first)
        /// @note  Stamped under the lock, so a prune never takes an entry being handed out.
        Entry& EntryOf(RE::FormID actor, Clock::time_point now, float& dt)
        {
            Table& t = T();
            std::scoped_lock guard(t.lock);
            if (t.byActor.size() > 256)
                std::erase_if(t.byActor, [now](const auto& kv) { return now - kv.second->last > std::chrono::seconds(5); });
            std::unique_ptr<Entry>& e = t.byActor[actor];
            if (!e) e = std::make_unique<Entry>();
            dt      = e->last == Clock::time_point{} ? 0.0f : std::clamp(std::chrono::duration<float>(now - e->last).count(), 0.0f, 0.25f);
            e->last = now;
            return *e;
        }

        /// @return the commonest nonzero material among the profile's floor, 0 with none
        std::uint32_t CommonMaterial(std::span<const lc::ProfileSample> profile)
        {
            std::uint32_t best = 0;
            int           most = 0;
            for (const lc::ProfileSample& a : profile) {
                if (!a.hit || !a.material) continue;
                int n = 0;
                for (const lc::ProfileSample& b : profile) n += b.hit && b.material == a.material ? 1 : 0;
                if (n > most) most = n, best = a.material;
            }
            return best;
        }

        std::string MaterialText(std::uint32_t id)
        {
            if (!id) return "none";
            const std::string_view name = hky::relay::MaterialName(id);
            return name.empty() ? std::format("{:#010x}", id) : std::string(name);
        }

        /// @brief The survey's line per flight: when the player first holds one, and when it lets go.
        void LogFlight(Entry& e, const lc::StairsReading& r, const Hull& hull, std::uint32_t material)
        {
            const bool flight = r.on || r.ahead;
            if (flight == e.flight) return;
            e.flight = flight;
            if (g_logged.fetch_add(1, std::memory_order_relaxed) >= kLogBudget) return;
            const auto* player = RE::PlayerCharacter::GetSingleton();
            const auto* cell   = player ? player->GetParentCell() : nullptr;
            const char* where  = cell && cell->GetFormEditorID() ? cell->GetFormEditorID() : "";
            const float x = hull.cx * kGameUnitsPerHavok, y = hull.cy * kGameUnitsPerHavok, z = hull.feetZ * kGameUnitsPerHavok;
            if (!flight) {
                LOG_INFO("stairs: flight let go at ({:.0f}, {:.0f}, {:.0f}) {}", x, y, z, where);
                return;
            }
            LOG_INFO("stairs: flight {} {} {}: {} risers, rise {:.1f}, tread {:.1f}, {}, confidence {:.2f}, material {} at ({:.0f}, {:.0f}, "
                     "{:.0f}) {}",
                     r.direction > 0 ? "up" : "down", r.on ? "on" : "ahead", r.on ? std::string() : std::format("in {:.0f}", r.distance), r.steps,
                     r.rise, r.tread, r.ramp ? "ramp-collision" : "stepped", r.confidence, MaterialText(material), x, y, z, where);
        }

        void PublishHud(const lc::StairsDetector& d, std::uint32_t material)
        {
            hud::Stairs h;
            const auto samples = d.March().Samples();
            const auto kinds   = d.Kinds();
            const int  first   = (std::max)(0, static_cast<int>(samples.size()) - hud::Stairs::kSamples);
            for (int i = first; i < static_cast<int>(samples.size()); i++) {
                const lc::MarchSample& m = samples[i];
                h.at[h.count]   = { m.x / kGameUnitsPerHavok, m.y / kGameUnitsPerHavok, m.z / kGameUnitsPerHavok };
                h.kind[h.count] = i < static_cast<int>(kinds.size()) ? static_cast<hud::Stairs::Kind>(kinds[i]) : hud::Stairs::Kind::None;
                ++h.count;
            }
            const lc::StairsReading& r = d.Reading();
            h.on = r.on, h.ahead = r.ahead, h.ramp = r.ramp, h.direction = r.direction, h.steps = r.steps;
            h.distance = r.distance, h.rise = r.rise, h.tread = r.tread, h.confidence = r.confidence;
            h.rawOn = d.Raw().on, h.rawAhead = d.Raw().ahead, h.rays = d.Rays();
            const std::string name = MaterialText(material);
            std::snprintf(h.material, sizeof(h.material), "%s", name.c_str());
            hud::Publish(h);
        }

    }  // namespace

    void SetRulesAsk(bool ask) { g_rulesAsk.store(ask, std::memory_order_relaxed); }

    bool Wanted(bool player) { return g_rulesAsk.load(std::memory_order_relaxed) || (player && Survey()); }

    void Update(::bhkCharacterController& ctrl, const Hull& hull, const Step& step)
    {
        if (!hull.valid) return;
        CB_ZONE(zone, "stairs march");
        float  dt = 0.0f;
        Entry& e  = EntryOf(step.actor, Clock::now(), dt);

        const platform::physics::GameWorld world(hull.world, kMarchTag);
        std::vector<ph::Vec3>              path;
        path.reserve(step.path.size());
        for (const hud::Point& p : step.path) path.push_back({ p.x * kGameUnitsPerHavok, p.y * kGameUnitsPerHavok, p.z * kGameUnitsPerHavok });
        lc::StairsMarchQuery q;
        q.world               = &world;
        q.filter.native       = ctrl.GetCollisionFilterInfo().m_filter;
        q.filter.ignoreLayers = step.ignoreLayers;
        q.stepHeight          = ctrl.m_stepHeight * kGameUnitsPerHavok;
        q.feet                = { hull.cx * kGameUnitsPerHavok, hull.cy * kGameUnitsPerHavok, hull.feetZ * kGameUnitsPerHavok };
        q.path                = path;
        q.dirX                = step.dirX;
        q.dirY                = step.dirY;
        q.speed               = step.speed * kGameUnitsPerHavok;
        const lc::StairsReading& r = e.detector.Update(q, dt, Tunables());

        hky::relay::StairsReading out;
        out.on = r.on, out.ahead = r.ahead, out.direction = r.direction, out.distance = r.distance;
        out.rise = r.rise, out.tread = r.tread, out.steps = r.steps;
        surface::SetStairs(step.actor, out);

        if (step.player && Survey()) {
            const std::uint32_t material = CommonMaterial(e.detector.March().Profile());
            LogFlight(e, r, hull, material);
            if (hud::On()) PublishHud(e.detector, material);
        }
    }

    float SignedRise(RE::FormID actor)
    {
        hky::relay::StairsReading r;
        if (!surface::Stairs(actor, r) || !r.on) return 0.0f;
        return r.rise * static_cast<float>(r.direction);
    }

}  // namespace CB::feature::controller::stairs
