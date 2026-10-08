/// @file
/// @brief The condition layer's engine seam: two subscribers on the setdata hook points
///        (<Hooks/points/SetData.h>). The weapon producer stashes its Actor* in TLS for the matcher call it
///        makes; the matcher subscriber appends our sensor {name, value} pairs to the assignment array via
///        save / repoint / restore, then continues the chain.
/// @note Inert until config loading registers ConditionInstances (Inc 2).
#include "Conditions.h"
#include <PluginLogger.h>

#include <Hooks/points/SetData.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace CB::core::conditions {

    namespace {
        using MatchNext = CB::core::hooks::SetDataGateMatcher::Next;
        using ProdNext  = CB::core::hooks::SetDataGateProducerWeapon::Next;

        // 0x10-stride assignment entry (POD copy of the engine's {BSFixedString name, int value}).
        struct AsgEntry { const void* name; std::int32_t value; std::uint32_t pad; };
        static_assert(sizeof(AsgEntry) == 0x10, "assignment entry must be 16 bytes");

        std::atomic<bool> s_enabled{ true };
        std::atomic<bool> s_installed{ false };

        thread_local RE::Actor* t_actor = nullptr;

        // Runtime condition instances (populated by config compose in Inc 2; empty → hook is inert).
        std::vector<ConditionInstance>& Instances() { static std::vector<ConditionInstance> v; return v; }

        // Evaluate all enabled instances against `actor`, producing sensor entries. One EvalContext per
        // pass so primitives memoize across every condition on this actor.
        std::size_t EvalSensors(RE::Actor* actor, std::vector<AsgEntry>& out) {
            static thread_local EvalContext ctx;
            ctx.Reset(actor, nullptr);
            out.clear();
            for (const ConditionInstance& inst : Instances()) {
                if (inst.enabled && !*inst.enabled) continue;              // per-config toggle
                if (!inst.expr.Valid()) continue;
                ctx.currentConfigId = inst.configId;                      // seeds Random() for this config
                const std::int32_t v = inst.expr.Evaluate(ctx).AsInt();   // bool → 0/1, int passthrough
                out.push_back({ inst.gateName.c_str(), v, 0 });           // interned name ptr, verified compare
            }
            return out.size();
        }

        // ── matcher hook: splice sensor pairs into the assignment array, then delegate ─────────────
        std::uint8_t OnMatch(MatchNext next, void* table, void* event, void* project, std::uintptr_t asgn, void* out) {
            RE::Actor* actor = t_actor;
            if (!s_enabled.load(std::memory_order_relaxed) || !actor || Instances().empty())
                return next(table, event, project, asgn, out);

            static thread_local std::vector<AsgEntry> extra;
            if (EvalSensors(actor, extra) == 0)
                return next(table, event, project, asgn, out);

            auto* const pData  = reinterpret_cast<void**>(asgn + 0x08);
            auto* const pCount = reinterpret_cast<std::uint32_t*>(asgn + 0x18);
            void* const    origData  = *pData;
            const std::uint32_t origCount = *pCount;

            static thread_local std::vector<AsgEntry> buf;
            buf.resize(static_cast<std::size_t>(origCount) + extra.size());
            if (origCount) std::memcpy(buf.data(), origData, static_cast<std::size_t>(origCount) * 0x10);
            std::memcpy(buf.data() + origCount, extra.data(), extra.size() * 0x10);

            *pData  = buf.data();
            *pCount = origCount + static_cast<std::uint32_t>(extra.size());
            const std::uint8_t r = next(table, event, project, asgn, out);
            *pData  = origData;             // restore BEFORE the producer's RAII frees the original buffer
            *pCount = origCount;
            return r;
        }

        // ── producer hook: stash the Actor* (param_1) for the matcher call it makes ────────────────
        std::uint8_t OnProducer(ProdNext next, RE::Actor* actor, void* p2, void* p3, void* p4) {
            RE::Actor* const prev = t_actor;
            t_actor = actor;
            const std::uint8_t r = next(actor, p2, p3, p4);
            t_actor = prev;                 // restore (nested/reentrant safe; non-hooked producers see null)
            return r;
        }
    }  // namespace

    // ── public control ──────────────────────────────────────────────────────────────────────────
    void SetGlobalEnabled(bool on) { s_enabled.store(on, std::memory_order_relaxed); }
    bool GlobalEnabled()           { return s_enabled.load(std::memory_order_relaxed); }

    void RegisterConditionInstance(ConditionInstance inst) { Instances().push_back(std::move(inst)); }
    void ClearConditionInstances()                         { Instances().clear(); }

    void InstallConditionHooks() {
        if (s_installed.exchange(true)) return;
        CB::core::hooks::SetDataGateProducerWeapon::Subscribe(&OnProducer, CB::core::hooks::Priority::Normal, "Conditions");
        CB::core::hooks::SetDataGateMatcher::Subscribe(&OnMatch, CB::core::hooks::Priority::Normal, "Conditions");
        LOG_INFO("[cond] subscribed to the setdata producer + matcher hook points; {} instance(s).", Instances().size());
    }

}  // namespace CB::core::conditions
