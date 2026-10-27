#include "PCH.h"

#include "DamageModifier.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/game/Combat.h>
#include <platform/hooks/CallSite.h>
#include <platform/hooks/Override.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <new>
#include <unordered_map>

/// @file
/// @brief CBDamageModifier's behavior and the five engine seams that read it. Each active, enabled node publishes its
///        members into a table keyed by node; at a hit, a magic check, a stagger or a health change the seams look up
///        the actor's characters there.
/// @note  Why a table and not a walk of the actor's active nodes: graphs update on several job threads at once, and
///        a walk from the game thread could read an active-node array mid-rebuild.
/// @note  Registered whatever the flags: a behavior holding this class must load, and an unregistered class name loads
///        with a null vtable.

namespace CB::feature::combat {

    namespace {

        namespace hk    = CB::havok;
        namespace hooks = platform::hooks;
        namespace game  = platform::game;

        constexpr const char* kClassName = "CBDamageModifier";

        void Finish(void* object)
        {
            if (object) new (object) DamageModifier(hk::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<DamageModifier*>(object)->~DamageModifier(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(DamageModifier) unsigned char scratch[sizeof(DamageModifier)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }

        [[maybe_unused]] const bool s_declared =
            platform::classes::Declare({ kClassName, sizeof(DamageModifier), &Finish, &Cleanup, &Vtable });

        /// @brief What one active node asks of its character.
        struct Published {
            const void* character   = nullptr;   ///< the hkbCharacter its graph runs (BShkbAnimationGraph::characterInstance)
            bool        ignoreHits  = false;
            bool        ignoreMagic = false;
            float       staggerMult = 1.0f;
            float       damageMult  = 1.0f;
        };

        struct Table {
            std::mutex                                             lock;
            std::unordered_map<const DamageModifier*, Published>   active;
            /// @brief active.size(), readable without the lock: the seams skip everything while no node is active.
            std::atomic<std::size_t>                               count{ 0 };
        };

        /// @note Never destroyed: a node destructor may still retract during static destruction at exit.
        Table& Active()
        {
            static Table* table = new Table;
            return *table;
        }

        void Publish(const DamageModifier& node, const hk::hkbContext& context)
        {
            Table&           t = Active();
            std::scoped_lock lock(t.lock);
            t.active[&node] = { context.m_character, node.m_ignoreHits, node.m_ignoreMagic, node.m_incomingStaggerMult,
                                node.m_incomingDamageMult };
            t.count.store(t.active.size(), std::memory_order_relaxed);
        }

        void Retract(const DamageModifier& node)
        {
            Table&           t = Active();
            std::scoped_lock lock(t.lock);
            if (t.active.erase(&node)) t.count.store(t.active.size(), std::memory_order_relaxed);
        }

        /// @brief Every active node on `actor`'s characters, combined: the flags OR, the multipliers multiply (each
        ///        clamped at 0, so a negative or NaN binding means immune rather than a heal).
        Published EffectOn(const RE::Actor* actor)
        {
            Published e;
            Table&    t = Active();
            if (!actor || t.count.load(std::memory_order_relaxed) == 0) return e;

            std::array<const void*, 4>                       characters{};
            std::size_t                                      n = 0;
            RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
            if (!actor->GetAnimationGraphManager(manager) || !manager) return e;
            for (const auto& graph : manager->graphs)
                if (graph && n < characters.size()) characters[n++] = &graph->characterInstance;
            const auto mine = [&](const void* character) {
                return std::find(characters.begin(), characters.begin() + n, character) != characters.begin() + n;
            };

            std::scoped_lock lock(t.lock);
            for (const auto& [node, p] : t.active) {
                if (!mine(p.character)) continue;
                e.ignoreHits  = e.ignoreHits || p.ignoreHits;
                e.ignoreMagic = e.ignoreMagic || p.ignoreMagic;
                e.staggerMult *= std::max(0.0f, p.staggerMult);
                e.damageMult  *= std::max(0.0f, p.damageMult);
            }
            return e;
        }

        std::atomic<int> g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 100; }

        using WeaponHit     = game::WeaponHitIsGhostCall;
        using ActorMagic    = game::ActorInvulnerableIsGhostCall;
        using PlayerMagic   = game::PlayerInvulnerableIsGhostCall;
        using Stagger       = hooks::Override<&game::Actor_Stagger>;
        using DoDamage      = hooks::Override<&game::Actor_DoDamage>;

        bool OnWeaponHit(WeaponHit::Next next, const RE::Actor* target)
        {
            if (EffectOn(target).ignoreHits) {
                if (LogBudget()) LOG_INFO("CBDamageModifier: a hit passes through {:08X}", target->GetFormID());
                return true;
            }
            return next(target);
        }

        bool IgnoresMagic(const RE::Actor* actor)
        {
            if (!EffectOn(actor).ignoreMagic) return false;
            if (LogBudget()) LOG_INFO("CBDamageModifier: {:08X} rejects magic", actor->GetFormID());
            return true;
        }

        bool OnActorMagic(ActorMagic::Next next, const RE::Actor* actor) { return IgnoresMagic(actor) || next(actor); }
        bool OnPlayerMagic(PlayerMagic::Next next, const RE::Actor* actor) { return IgnoresMagic(actor) || next(actor); }

        /// @note Scaled before vanilla's perk entries, so Mod Incoming Stagger still applies on top, and a multiplier of
        ///       0 lands on vanilla's own `<= 0` early return: no stagger, nothing else changed.
        void OnStagger(Stagger::Next next, RE::Actor* actor, float magnitude, RE::Actor* aggressor)
        {
            const float mult = EffectOn(actor).staggerMult;
            if (mult != 1.0f && LogBudget())
                LOG_INFO("CBDamageModifier: stagger {:.2f} x {:.2f} on {:08X}", magnitude, mult, actor ? actor->GetFormID() : 0);
            next(actor, magnitude * mult, aggressor);
        }

        /// @note Scaled before the difficulty multiplier, which DoDamage applies to whatever it is handed.
        bool OnDoDamage(DoDamage::Next next, RE::Actor* actor, float damage, RE::Actor* aggressor, bool dontAdjustDifficulty)
        {
            const float mult = EffectOn(actor).damageMult;
            if (mult != 1.0f && LogBudget())
                LOG_INFO("CBDamageModifier: damage {:.1f} x {:.2f} on {:08X}", damage, mult, actor ? actor->GetFormID() : 0);
            return next(actor, damage * mult, aggressor, dontAdjustDifficulty);
        }

        [[maybe_unused]] const bool s_weaponHit   = WeaponHit::Subscribe(&OnWeaponHit, hooks::Priority::Normal, "CBDamageModifier");
        [[maybe_unused]] const bool s_actorMagic  = ActorMagic::Subscribe(&OnActorMagic, hooks::Priority::Normal, "CBDamageModifier");
        [[maybe_unused]] const bool s_playerMagic = PlayerMagic::Subscribe(&OnPlayerMagic, hooks::Priority::Normal, "CBDamageModifier");
        [[maybe_unused]] const bool s_stagger     = Stagger::Subscribe(&OnStagger, hooks::Priority::Normal, "CBDamageModifier");
        [[maybe_unused]] const bool s_doDamage    = DoDamage::Subscribe(&OnDoDamage, hooks::Priority::Normal, "CBDamageModifier");

    }  // namespace

    DamageModifier::DamageModifier(hk::hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
        m_userData |= 2;
    }

    DamageModifier::DamageModifier(const DamageModifier& other)
    :   hkbModifier(other),
        m_ignoreHits(other.m_ignoreHits),
        m_ignoreMagic(other.m_ignoreMagic),
        m_incomingStaggerMult(other.m_incomingStaggerMult),
        m_incomingDamageMult(other.m_incomingDamageMult)
    {
        m_userData |= 2;
    }

    DamageModifier::~DamageModifier() { Retract(*this); }

    const void* DamageModifier::getClassType() const
    {
        static const void* const cls = platform::classes::ClassOf(kClassName);
        return cls;
    }

    void DamageModifier::activate(const hk::hkbContext& context) { Publish(*this, context); }

    void DamageModifier::update(const hk::hkbContext& context, hk::hkReal timestep) { Publish(*this, context); }

    void DamageModifier::deactivate(const hk::hkbContext& context) { Retract(*this); }

    bool DamageModifier::isValid(hk::hkStringPtr& errorString) const { return true; }

    hk::hkbNode* DamageModifier::cloneNode(hk::hkbBehaviorGraph& rootBehavior) const { return new DamageModifier(*this); }

}  // namespace CB::feature::combat
