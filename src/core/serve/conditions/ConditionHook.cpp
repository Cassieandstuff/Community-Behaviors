// ConditionHook.cpp — the runtime seam. Two MinHook function-entry hooks:
//   • the setdata PRODUCER (FUN_1407c5b00): param_1 is the Actor* → stash it in TLS for this queue.
//   • the setdata MATCHER  (FUN_14053c080): append our sensor {name,value} pairs to the assignment
//     array via save/repoint/restore, then call the original (see plan §Append contract).
//
// Verified layout: the assignment array (param_4) is BSTArray<{BSFixedString,int}> — matcher reads
// data @ +0x08, count(uint32) @ +0x18, entries stride 0x10 {name@+0, int32 value@+8}. Name match is
// by interned pointer. Inert until config loading registers ConditionInstances (Inc 2).
//
// RVAs below are AE 1.6.1170 (matching the repo's existing raw-RVA hooks); SE/VR need a REL::ID map.
#include "Conditions.h"
#include <PluginLogger.h>

#include <MinHook.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace CB::conditions {

    namespace {
        // ── engine ABI ────────────────────────────────────────────────────────────────────────────
        constexpr std::uintptr_t kMatcherRVA  = 0x53C080;   // FUN_14053c080
        constexpr std::uintptr_t kProducerRVA = 0x7C5B00;   // FUN_1407c5b00 (one of ~8; see open item)

        // 0x10-stride assignment entry (POD copy of the engine's {BSFixedString name, int value}).
        struct AsgEntry { const void* name; std::int32_t value; std::uint32_t pad; };
        static_assert(sizeof(AsgEntry) == 0x10, "assignment entry must be 16 bytes");

        using MatchFn = std::uint8_t (*)(void*, void*, void*, std::uintptr_t, void*);
        using ProdFn  = std::uint8_t (*)(RE::Actor*, void*, void*, void*);

        MatchFn s_origMatch = nullptr;
        ProdFn  s_origProd  = nullptr;

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
                const std::int32_t v = inst.expr.Evaluate(ctx).AsInt();   // bool → 0/1, int passthrough
                out.push_back({ inst.gateName.c_str(), v, 0 });           // interned name ptr, verified compare
            }
            return out.size();
        }

        // ── matcher hook: splice sensor pairs into the assignment array, then delegate ─────────────
        std::uint8_t Hook_Match(void* table, void* event, void* project, std::uintptr_t asgn, void* out) {
            RE::Actor* actor = t_actor;
            if (!s_enabled.load(std::memory_order_relaxed) || !actor || Instances().empty())
                return s_origMatch(table, event, project, asgn, out);

            static thread_local std::vector<AsgEntry> extra;
            if (EvalSensors(actor, extra) == 0)
                return s_origMatch(table, event, project, asgn, out);

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
            const std::uint8_t r = s_origMatch(table, event, project, asgn, out);
            *pData  = origData;             // restore BEFORE the producer's RAII frees the original buffer
            *pCount = origCount;
            return r;
        }

        // ── producer hook: stash the Actor* (param_1) for the matcher call it makes ────────────────
        std::uint8_t Hook_Producer(RE::Actor* actor, void* p2, void* p3, void* p4) {
            RE::Actor* const prev = t_actor;
            t_actor = actor;
            const std::uint8_t r = s_origProd(actor, p2, p3, p4);
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
        const MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            LOG_WARN("[cond] MH_Initialize failed ({}) — condition hooks NOT installed.", static_cast<int>(init));
            s_installed.store(false); return;
        }
        const std::uintptr_t base = REL::Module::get().base();
        void* const matcher  = reinterpret_cast<void*>(base + kMatcherRVA);
        void* const producer = reinterpret_cast<void*>(base + kProducerRVA);

        if (MH_CreateHook(matcher, reinterpret_cast<void*>(&Hook_Match),
                          reinterpret_cast<void**>(&s_origMatch)) != MH_OK ||
            MH_EnableHook(matcher) != MH_OK) {
            LOG_WARN("[cond] failed to install setdata matcher hook @ rva 0x{:X}.", kMatcherRVA);
            s_installed.store(false); return;
        }
        if (MH_CreateHook(producer, reinterpret_cast<void*>(&Hook_Producer),
                          reinterpret_cast<void**>(&s_origProd)) != MH_OK ||
            MH_EnableHook(producer) != MH_OK) {
            LOG_WARN("[cond] failed to install setdata producer hook @ rva 0x{:X}.", kProducerRVA);
            // matcher stays installed but inert without an actor; leave it.
            return;
        }
        LOG_INFO("[cond] condition hooks installed (matcher 0x{:X} + producer 0x{:X}); {} instance(s).",
                 kMatcherRVA, kProducerRVA, Instances().size());
    }

}  // namespace CB::conditions
