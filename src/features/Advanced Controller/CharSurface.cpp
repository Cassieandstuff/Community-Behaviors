#include "PCH.h"

#include "CharSurface.h"

#include <hky/relay/Surface.h>

#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CB::feature::controller::surface {

    namespace {

        using Clock = std::chrono::steady_clock;

        /// @brief A reading older than this reads 0: the character left management, unloaded, or its NPC prediction
        ///        stopped running.
        constexpr auto kStale = std::chrono::milliseconds(1000);

        /// @brief The log lines the player's material changes may take, so mixed ground cannot flood the log.
        constexpr int kLogBudget = 400;

        struct Entry {
            std::uint32_t     now = 0, ahead = 0;
            Clock::time_point nowAt{}, aheadAt{};
        };

        struct Table {
            std::shared_mutex                         lock;
            std::unordered_map<std::uint32_t, Entry>  byActor;
        };

        Table& T()
        {
            static Table t;
            return t;
        }

        std::atomic<int> g_logged{ 0 };

        /// @brief Logs the player's reading when it changes.
        void LogChange(std::uint32_t actor, const char* what, std::uint32_t before, std::uint32_t after)
        {
            if (before == after) return;
            const auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || player->GetFormID() != actor) return;
            if (g_logged.fetch_add(1, std::memory_order_relaxed) >= kLogBudget) return;
            const auto name = [](std::uint32_t id) {
                const std::string_view n = hky::relay::MaterialName(id);
                return n.empty() ? std::format("{:#010x}", id) : std::string(n);
            };
            LOG_INFO("surface: {} {} -> {}", what, before ? name(before) : std::string("none"), after ? name(after) : std::string("none"));
        }

        /// @brief Drops readings gone stale, so characters that left do not pile up.
        void Prune(Table& t, Clock::time_point now)
        {
            if (t.byActor.size() < 256) return;
            std::erase_if(t.byActor, [now](const auto& kv) { return now - kv.second.nowAt > kStale && now - kv.second.aheadAt > kStale; });
        }

        hky::relay::SurfaceReading Read(const hky::relay::EvalContext& ctx)
        {
            const auto* actor = static_cast<const RE::Actor*>(ctx.actor);
            if (!actor) return {};
            const auto now = Clock::now();
            Table&     t   = T();
            std::shared_lock guard(t.lock);
            const auto it = t.byActor.find(actor->GetFormID());
            if (it == t.byActor.end()) return {};
            hky::relay::SurfaceReading r;
            if (now - it->second.nowAt <= kStale) r.now = it->second.now;
            if (now - it->second.aheadAt <= kStale) r.ahead = it->second.ahead;
            return r;
        }

    }  // namespace

    void SetNow(std::uint32_t actor, std::uint32_t material)
    {
        const auto now = Clock::now();
        Table&     t   = T();
        std::uint32_t before;
        {
            std::unique_lock guard(t.lock);
            Prune(t, now);
            Entry& e = t.byActor[actor];
            before   = now - e.nowAt <= kStale ? e.now : 0;
            e.now    = material;
            e.nowAt  = now;
        }
        LogChange(actor, "under the feet", before, material);
    }

    void SetAhead(std::uint32_t actor, std::uint32_t material)
    {
        const auto now = Clock::now();
        Table&     t   = T();
        std::uint32_t before;
        {
            std::unique_lock guard(t.lock);
            Prune(t, now);
            Entry& e  = t.byActor[actor];
            before    = now - e.aheadAt <= kStale ? e.ahead : 0;
            e.ahead   = material;
            e.aheadAt = now;
        }
        LogChange(actor, "ahead", before, material);
    }

    void Bind() { hky::relay::SetSurfaceSource(&Read); }

}  // namespace CB::feature::controller::surface
