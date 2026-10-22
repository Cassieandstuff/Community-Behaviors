#include "PCH.h"

#include "CharSkeletonWatch.h"

#include "CharMotionMatching.h"

#include <platform/profile/Profile.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CB::feature::controller::motion {

    namespace {

        using Clock = std::chrono::steady_clock;

        /// @brief The watched bones, by their skeleton names (skeleton.hkx's bone list; the nif's nodes carry the same).
        /// @note  NPC Root first: an actor without it has no humanoid skeleton and is not watched.
        constexpr std::array<std::string_view, 4> kBones = { "NPC Root [Root]", "NPC Pelvis [Pelv]", "NPC Head [Head]",
                                                             "Camera3rd [Cam3]" };
        constexpr float  kMaxReach = 1000.0f;   ///< game units a watched bone may be from its actor
        constexpr double kNpcEvery = 0.25;      ///< seconds between passes over the high-process actors
        constexpr int    kMaxLines = 200;       ///< log lines the watch writes per session (Tracy gets every one)

        std::atomic<bool> g_queued{ false };
        std::atomic<bool> g_looked{ false };
        /// @brief The player's graph characters as the last pass saw them (the first- and third-person graphs).
        std::array<std::atomic<const void*>, 2> g_playerCharacters{};

        /// @note Main thread only (the SKSE task).
        struct Watch {
            std::unordered_map<RE::FormID, bool> failing;   ///< per actor: inside a failure episode
            int                                  lines = 0;
            Clock::time_point                    lastNpc{};
        };
        Watch& State()
        {
            static Watch w;
            return w;
        }

        bool Finite(const RE::NiPoint3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

        void CachePlayerCharacters(RE::PlayerCharacter* player)
        {
            std::array<const void*, 2>                       found{};
            RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
            if (player->GetAnimationGraphManager(manager) && manager) {
                std::size_t n = 0;
                for (const auto& graph : manager->graphs)
                    if (graph && n < found.size()) found[n++] = &graph->characterInstance;
            }
            for (std::size_t i = 0; i < found.size(); ++i) g_playerCharacters[i].store(found[i], std::memory_order_relaxed);
            g_looked.store(true, std::memory_order_release);
        }

        /// @return what is wrong with the skeleton under `root`, empty when nothing is or it is not a humanoid's
        std::string Check(RE::Actor* actor, RE::NiAVObject* root)
        {
            static const std::array<RE::BSFixedString, 4> names = { RE::BSFixedString(kBones[0]), RE::BSFixedString(kBones[1]),
                                                                    RE::BSFixedString(kBones[2]), RE::BSFixedString(kBones[3]) };
            std::array<RE::NiAVObject*, 4> bones{};
            for (std::size_t i = 0; i < bones.size(); ++i) bones[i] = root->GetObjectByName(names[i]);
            if (!bones[0]) return {};
            const RE::NiPoint3 at = actor->GetPosition();
            std::string        why;
            const auto         add = [&](const std::string& s) { why += why.empty() ? s : "; " + s; };
            for (std::size_t i = 0; i < bones.size(); ++i) {
                if (!bones[i]) {
                    add(std::format("{} missing", kBones[i]));
                    continue;
                }
                const RE::NiPoint3& p = bones[i]->world.translate;
                const bool          ok = Finite(p);
                const float         d  = ok ? p.GetDistance(at) : NAN;
                if (ok && d <= kMaxReach) continue;
                add(std::format("{} at ({:.1f}, {:.1f}, {:.1f}), {:.0f} from the actor", kBones[i], p.x, p.y, p.z, d));
            }
            /// @note A non-finite world bound fails every culling test, so the actor would vanish without A-posing.
            const RE::NiBound& b = root->worldBound;
            if (!Finite(b.center) || !std::isfinite(b.radius))
                add(std::format("world bound ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}", b.center.x, b.center.y, b.center.z, b.radius));
            return why;
        }

        void Visit(RE::Actor* actor, Watch& w)
        {
            RE::NiAVObject*   root    = actor->Get3D(false);
            const std::string why     = root ? Check(actor, root) : std::string{};
            bool&             failing = w.failing[actor->GetFormID()];
            if (why.empty()) {
                if (failing && w.lines++ < kMaxLines)
                    LOG_INFO("skeleton watch: {} ({:08X}) recovered", actor->GetDisplayFullName(), actor->GetFormID());
                failing = false;
                return;
            }
            if (failing) return;
            failing                = true;
            const RE::NiPoint3 pos = actor->GetPosition();
            const std::string  line =
                std::format("skeleton watch: {} ({:08X}) {}; actor at ({:.0f}, {:.0f}, {:.0f}), 3D {}; motion matching: {}",
                            actor->GetDisplayFullName(), actor->GetFormID(), why, pos.x, pos.y, pos.z,
                            root->GetAppCulled() ? "culled" : "not culled", Describe());
            if (w.lines++ < kMaxLines) LOG_WARN("{}", line);
            CB_MESSAGE("{}", line);
        }

        void Pass()
        {
            g_queued.store(false, std::memory_order_release);
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;
            CB_ZONE(zone, "skeleton watch");
            CachePlayerCharacters(player);
            Watch& w = State();
            Visit(player, w);
            const auto now = Clock::now();
            if (std::chrono::duration<double>(now - w.lastNpc).count() < kNpcEvery) return;
            w.lastNpc   = now;
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) return;
            for (const RE::ActorHandle& handle : lists->highActorHandles) {
                const RE::NiPointer<RE::Actor> actor = handle.get();
                if (actor && actor.get() != player) Visit(actor.get(), w);
            }
        }

    }  // namespace

    void QueueSkeletonWatch()
    {
        if (g_queued.exchange(true, std::memory_order_acq_rel)) return;
        if (auto* tasks = SKSE::GetTaskInterface())
            tasks->AddTask([] { Pass(); });
        else
            g_queued.store(false, std::memory_order_release);
    }

    std::optional<bool> IsPlayerCharacter(const void* character)
    {
        if (!g_looked.load(std::memory_order_acquire)) return std::nullopt;
        for (const auto& c : g_playerCharacters)
            if (character && c.load(std::memory_order_relaxed) == character) return true;
        return false;
    }

}  // namespace CB::feature::controller::motion
