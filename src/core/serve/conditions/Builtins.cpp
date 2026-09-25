// Builtins.cpp — the BUILT-IN primitive/function roster.
//
// ┌─────────────────────────────────────────────────────────────────────────────────────────────┐
// │ SEAM: this file is the roster branch. It implements CB's OAR-parity condition vocabulary as    │
// │ the two lower layers of the three-layer system (see ~/.claude/plans/setdata-conditional-       │
// │ framework.md §5/§5a):                                                                          │
// │   PRIMITIVES — the ONLY layer that reads game state; memoized once per (id,args) per eval pass. │
// │   FUNCTIONS  — PURE over primitives (`c.Primitive(...)`), never touch game state directly.      │
// │ Composition is free in the expression grammar, so we ship only value-returning + boolean       │
// │ functions and let the grammar do `< == && …`; we never add per-comparison functions.           │
// │                                                                                                │
// │ Every CommonLib accessor below is verified against the pinned alandtse `ng` submodule           │
// │ (lib/commonlibsse-ng @ 5decf47). Cross-VR: game-state reads go through the versioned casts       │
// │ (AsActorValueOwner/AsMagicTarget/GetActorRuntimeData) CommonLib resolves per runtime.           │
// └─────────────────────────────────────────────────────────────────────────────────────────────┘
#include "Conditions.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace CB::conditions {

    namespace {

        // ── Hand convention ─────────────────────────────────────────────────────────────────────
        // Equipped-hand args: 1 = LEFT hand, anything else (0) = RIGHT hand. Matches GetEquippedObject's
        // bool leftHand and the OAR/DAR "Left hand" flag; the converter maps OAR's bool onto this.
        constexpr bool IsLeftHand(std::int32_t hand) { return hand == 1; }

        // ── MoveFlags bitfield ──────────────────────────────────────────────────────────────────
        // A single memoized snapshot of the actor's movement/pose state, so every Is<State>() predicate
        // reduces to one bit-test over ONE game-state read. The "…" in the spec's "MoveFlags(bitfield:
        // sneaking/running/sprinting/swimming/inAir/…)" — folded the pose/mount/vitals bits in here too
        // so those predicates stay pure-over-primitive without a primitive-per-flag sprawl.
        enum MoveBit : std::int32_t {
            kMB_Sneaking    = 1 << 0,
            kMB_Running     = 1 << 1,
            kMB_Sprinting   = 1 << 2,
            kMB_Swimming    = 1 << 3,
            kMB_InAir       = 1 << 4,
            kMB_Walking     = 1 << 5,
            kMB_Sitting     = 1 << 6,
            kMB_Sleeping    = 1 << 7,
            kMB_Mounted     = 1 << 8,
            kMB_Flying      = 1 << 9,
            kMB_BleedingOut = 1 << 10,
        };

        // ── Small shared helpers over memoized primitives (functions call these, never game state) ──
        bool KeywordArrayHas(const EvalContext& c, PrimId arrayPrim, RE::BGSKeyword* kw) {
            if (!kw || arrayPrim == PrimId::Invalid) return false;
            const Value v = c.Primitive(arrayPrim);
            for (const Value& e : v.arr)
                if (e.kw == kw) return true;
            return false;
        }

        bool FormArrayHas(const EvalContext& c, PrimId arrayPrim, RE::TESForm* form) {
            if (!form || arrayPrim == PrimId::Invalid) return false;
            const Value v = c.Primitive(arrayPrim);
            for (const Value& e : v.arr)
                if (e.form == form) return true;
            return false;
        }

        std::int32_t MoveFlagsOf(const EvalContext& c) {
            static const PrimId kMoveFlags = LookupPrimitive("MoveFlags");
            return c.Primitive(kMoveFlags).AsInt();
        }

        // ═══════════════════════════════════════════════════════════════════════════════════════════
        //  PRIMITIVES  (game-state reads; memoized once per (id,args) per pass by the framework)
        // ═══════════════════════════════════════════════════════════════════════════════════════════

        // Keywords -> Keyword[]  (the actor's base/template keywords)
        Value Prim_Keywords(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor)
                if (auto* npc = actor->GetActorBase())
                    for (RE::BGSKeyword* kw : npc->GetKeywords())
                        if (kw) block.push_back(Value::Keyword(kw));
            return Value::Array(VType::KeywordArray, block);
        }

        // WornKeywords -> Keyword[]  (union of keywords across all worn armor pieces)
        Value Prim_WornKeywords(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor) {
                constexpr std::size_t kMaxSeen = 16;
                RE::TESObjectARMO* seenArmo[kMaxSeen] = {};   // dedup: multi-slot armor repeats across bits
                std::size_t seenN = 0;
                for (std::uint32_t bit = 0; bit < 32; ++bit) {
                    const auto slot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << bit);
                    RE::TESObjectARMO* armo = actor->GetWornArmor(slot);
                    if (!armo) continue;
                    bool seen = false;
                    for (std::size_t i = 0; i < seenN; ++i)
                        if (seenArmo[i] == armo) { seen = true; break; }
                    if (seen) continue;
                    if (seenN < kMaxSeen) seenArmo[seenN++] = armo;
                    for (RE::BGSKeyword* kw : armo->GetKeywords())
                        if (kw) block.push_back(Value::Keyword(kw));
                }
            }
            return Value::Array(VType::KeywordArray, block);
        }

        // EquippedForm(hand) -> Form
        Value Prim_EquippedForm(const EvalContext& c, std::span<const Value> a) {
            if (!c.actor || a.empty()) return Value::Form(nullptr);
            return Value::Form(c.actor->GetEquippedObject(IsLeftHand(a[0].AsInt())));
        }

        // EquippedType(hand) -> Int   (RE::WEAPON_TYPE: 0 = hand-to-hand/unarmed … 9 = crossbow; -1 = non-weapon)
        Value Prim_EquippedType(const EvalContext& c, std::span<const Value> a) {
            if (!c.actor || a.empty()) return Value::Int(0);
            RE::TESForm* form = c.actor->GetEquippedObject(IsLeftHand(a[0].AsInt()));
            if (!form) return Value::Int(0);   // empty hand = unarmed hand-to-hand
            if (auto* weap = form->As<RE::TESObjectWEAP>())
                return Value::Int(static_cast<std::int32_t>(weap->GetWeaponType()));
            return Value::Int(-1);             // equipped but not a weapon
        }

        // ActorValue(av) -> Float
        Value Prim_ActorValue(const EvalContext& c, std::span<const Value> a) {
            if (!c.actor || a.empty()) return Value::Float(0.0f);
            if (auto* avo = c.actor->AsActorValueOwner())
                return Value::Float(avo->GetActorValue(static_cast<RE::ActorValue>(a[0].AsInt())));
            return Value::Float(0.0f);
        }

        // ActorBase -> Form
        Value Prim_ActorBase(const EvalContext& c, std::span<const Value>) {
            return Value::Form(c.actor ? c.actor->GetActorBase() : nullptr);
        }

        // Race -> Form
        Value Prim_Race(const EvalContext& c, std::span<const Value>) {
            return Value::Form(c.actor ? c.actor->GetRace() : nullptr);
        }

        // Factions -> Form[]   (factions the actor is a member of, rank >= 0)
        Value Prim_Factions(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor) {
                std::vector<Value>* out = &block;
                actor->VisitFactions([out](RE::TESFaction* faction, std::int8_t rank) -> bool {
                    if (faction && rank >= 0) out->push_back(Value::Form(faction));
                    return false;   // false = keep visiting
                });
            }
            return Value::Array(VType::FormArray, block);
        }

        // Perks -> Form[]   (the character-base perk roster; NOTE: runtime-added perks are not enumerated
        // here — HasPerk() is authoritative via the Actor::HasPerk read below).
        Value Prim_Perks(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor) {
                if (auto* base = actor->GetActorBase()) {
                    const RE::BGSPerkRankArray* pra = base;   // TESNPC -> TESActorBase -> BGSPerkRankArray
                    if (pra->perks)
                        for (std::uint32_t i = 0; i < pra->perkCount; ++i)
                            if (pra->perks[i].perk) block.push_back(Value::Form(pra->perks[i].perk));
                }
            }
            return Value::Array(VType::FormArray, block);
        }

        // HasPerk(form) -> Int   (supporting predicate-primitive: authoritative incl. runtime-added perks)
        Value Prim_HasPerk(const EvalContext& c, std::span<const Value> a) {
            if (!c.actor || a.empty()) return Value::Int(0);
            auto* perk = a[0].form ? a[0].form->As<RE::BGSPerk>() : nullptr;
            return Value::Int(perk && c.actor->HasPerk(perk) ? 1 : 0);
        }

        // Spells -> Form[]   (all spells the actor knows — base + added, via the engine visitor)
        Value Prim_Spells(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor) {
                struct Collector : RE::Actor::ForEachSpellVisitor {
                    std::vector<Value>* out = nullptr;
                    RE::BSContainer::ForEachResult Visit(RE::SpellItem* spell) override {
                        if (spell) out->push_back(Value::Form(spell));
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                };
                Collector visitor;              // ForEachSpellVisitor is polymorphic → not an aggregate
                visitor.out = &block;
                actor->VisitSpells(visitor);
            }
            return Value::Array(VType::FormArray, block);
        }

        // MagicEffects -> Form[]   (base EffectSetting of every active effect)
        Value Prim_MagicEffects(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor)
                if (auto* mt = actor->AsMagicTarget())
                    if (auto* list = mt->GetActiveEffectList())
                        for (RE::ActiveEffect* ae : *list)
                            if (ae)
                                if (auto* base = ae->GetBaseObject())
                                    block.push_back(Value::Form(base));
            return Value::Array(VType::FormArray, block);
        }

        // MagicEffectKeywords -> Keyword[]   (union of keywords across active-effect base settings)
        Value Prim_MagicEffectKeywords(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor)
                if (auto* mt = actor->AsMagicTarget())
                    if (auto* list = mt->GetActiveEffectList())
                        for (RE::ActiveEffect* ae : *list)
                            if (ae)
                                if (auto* base = ae->GetBaseObject())
                                    for (RE::BGSKeyword* kw : base->GetKeywords())
                                        if (kw) block.push_back(Value::Keyword(kw));
            return Value::Array(VType::KeywordArray, block);
        }

        // Level -> Int
        Value Prim_Level(const EvalContext& c, std::span<const Value>) {
            return Value::Int(c.actor ? static_cast<std::int32_t>(c.actor->GetLevel()) : 0);
        }

        // Sex -> Int   (0 male, 1 female; -1 none)
        Value Prim_Sex(const EvalContext& c, std::span<const Value>) {
            if (auto* actor = c.actor)
                if (auto* npc = actor->GetActorBase())
                    return Value::Int(static_cast<std::int32_t>(npc->GetSex()));
            return Value::Int(-1);
        }

        // MoveFlags -> Int   (see MoveBit above)
        Value Prim_MoveFlags(const EvalContext& c, std::span<const Value>) {
            std::int32_t flags = 0;
            if (auto* actor = c.actor) {
                if (auto* st = actor->AsActorState()) {
                    if (st->IsSneaking())    flags |= kMB_Sneaking;
                    if (st->IsRunning())     flags |= kMB_Running;
                    if (st->IsSprinting())   flags |= kMB_Sprinting;
                    if (st->IsSwimming())    flags |= kMB_Swimming;
                    if (st->IsWalking())     flags |= kMB_Walking;
                    if (st->IsFlying())      flags |= kMB_Flying;
                    if (st->IsBleedingOut()) flags |= kMB_BleedingOut;
                    const auto ss = st->GetSitSleepState();
                    if (ss == RE::SIT_SLEEP_STATE::kIsSitting)  flags |= kMB_Sitting;
                    if (ss == RE::SIT_SLEEP_STATE::kIsSleeping) flags |= kMB_Sleeping;
                }
                if (actor->IsInMidair()) flags |= kMB_InAir;
                if (actor->IsOnMount())  flags |= kMB_Mounted;
            }
            return Value::Int(flags);
        }

        // Weather -> Form
        Value Prim_Weather(const EvalContext&, std::span<const Value>) {
            auto* sky = RE::Sky::GetSingleton();
            return Value::Form(sky ? sky->currentWeather : nullptr);
        }

        // GameTime -> Float   (current hour, 0..24)
        Value Prim_GameTime(const EvalContext&, std::span<const Value>) {
            auto* cal = RE::Calendar::GetSingleton();
            return Value::Float(cal ? cal->GetHour() : 0.0f);
        }

        // Mount -> Form
        Value Prim_Mount(const EvalContext& c, std::span<const Value>) {
            if (auto* actor = c.actor) {
                RE::NiPointer<RE::Actor> mount;
                if (actor->GetMount(mount) && mount)
                    return Value::Form(mount.get());
            }
            return Value::Form(nullptr);
        }

        // CombatState -> Int   (RE::ACTOR_COMBAT_STATE: 0 = none, 1 = in-combat)
        Value Prim_CombatState(const EvalContext& c, std::span<const Value>) {
            return Value::Int(c.actor && c.actor->IsInCombat() ? 1 : 0);
        }

        // GraphVar(name) -> Float
        // LIMITATION (Inc 1 ABI): the Value union has no String type, so a graph-variable NAME literal
        // can't be carried cleanly yet. Best-effort: the arg is a Keyword whose EditorID is used as the
        // variable name. TODO(Inc 2/3): add a String VType to Value (Conditions.h) so authors can pass a
        // bare variable name, and resolve it in the ArgResolver.
        Value Prim_GraphVar(const EvalContext& c, std::span<const Value> a) {
            if (!c.actor || a.empty() || a[0].type != VType::Keyword || !a[0].kw)
                return Value::Float(0.0f);
            const RE::BSFixedString name{ a[0].kw->GetFormEditorID() };
            float out = 0.0f;
            c.actor->GetGraphVariableFloat(name, out);
            return Value::Float(out);
        }

        // Global(form) -> Float
        Value Prim_Global(const EvalContext&, std::span<const Value> a) {
            if (!a.empty() && a[0].form)
                if (auto* g = a[0].form->As<RE::TESGlobal>())
                    return Value::Float(g->value);
            return Value::Float(0.0f);
        }

        // FactionRank(faction) -> Int   (supporting primitive for the FactionRank() value-function; -2 = not in)
        Value Prim_FactionRank(const EvalContext& c, std::span<const Value> a) {
            if (!c.actor || a.empty()) return Value::Int(-2);
            auto* faction = a[0].form ? a[0].form->As<RE::TESFaction>() : nullptr;
            if (!faction) return Value::Int(-2);
            const bool isPlayer = c.actor->GetFormID() == 0x14;
            return Value::Int(c.actor->GetFactionRank(faction, isPlayer));
        }

        // Location -> Form
        Value Prim_Location(const EvalContext& c, std::span<const Value>) {
            return Value::Form(c.actor ? c.actor->GetCurrentLocation() : nullptr);
        }

        // LocationKeywords -> Keyword[]
        Value Prim_LocationKeywords(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor)
                if (auto* loc = actor->GetCurrentLocation())
                    for (RE::BGSKeyword* kw : loc->GetKeywords())
                        if (kw) block.push_back(Value::Keyword(kw));
            return Value::Array(VType::KeywordArray, block);
        }

        // Interior -> Int   (supporting primitive for IsInInterior(); 1 = interior cell)
        Value Prim_Interior(const EvalContext& c, std::span<const Value>) {
            if (auto* actor = c.actor)
                if (auto* cell = actor->GetParentCell())
                    return Value::Int(cell->IsInteriorCell() ? 1 : 0);
            return Value::Int(0);
        }

        // Child -> Int   (supporting primitive for IsChild())
        Value Prim_Child(const EvalContext& c, std::span<const Value>) {
            return Value::Int(c.actor && c.actor->IsChild() ? 1 : 0);
        }

        // Unique -> Int   (supporting primitive for IsUnique(); base-flag)
        Value Prim_Unique(const EvalContext& c, std::span<const Value>) {
            if (auto* actor = c.actor)
                if (auto* npc = actor->GetActorBase())
                    return Value::Int(npc->IsUnique() ? 1 : 0);
            return Value::Int(0);
        }

        // PlayerTeammate -> Int   (supporting primitive for IsPlayerTeammate())
        Value Prim_PlayerTeammate(const EvalContext& c, std::span<const Value>) {
            return Value::Int(c.actor && c.actor->IsPlayerTeammate() ? 1 : 0);
        }

        // ═══════════════════════════════════════════════════════════════════════════════════════════
        //  FUNCTIONS — value-returning (feed grammar comparisons like `GetAV(x) > 50`)
        // ═══════════════════════════════════════════════════════════════════════════════════════════

        Value Fn_GetAV(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("ActorValue");
            if (a.empty()) return Value::Float(0.0f);
            const std::array<Value, 1> args{ a[0] };
            return Value::Float(c.Primitive(p, args).AsFloat());
        }

        Value Fn_Level(const EvalContext& c, std::span<const Value>) {
            static const PrimId p = LookupPrimitive("Level");
            return Value::Int(c.Primitive(p).AsInt());
        }

        Value Fn_GameTime(const EvalContext& c, std::span<const Value>) {
            static const PrimId p = LookupPrimitive("GameTime");
            return Value::Float(c.Primitive(p).AsFloat());
        }

        Value Fn_FactionRank(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("FactionRank");
            if (a.empty()) return Value::Int(-2);
            const std::array<Value, 1> args{ a[0] };
            return Value::Int(c.Primitive(p, args).AsInt());
        }

        Value Fn_GraphVar(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("GraphVar");
            if (a.empty()) return Value::Float(0.0f);
            const std::array<Value, 1> args{ a[0] };
            return Value::Float(c.Primitive(p, args).AsFloat());
        }

        Value Fn_Global(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("Global");
            if (a.empty()) return Value::Float(0.0f);
            const std::array<Value, 1> args{ a[0] };
            return Value::Float(c.Primitive(p, args).AsFloat());
        }

        // Random() -> Float in [0,1), STABLE per (actor, config) for the session (spec §5a, PINNED):
        // a naive rand() re-rolls every animation queue and strobes. Seed = hash(actor->formID, configId).
        // TODO(configId): EvalContext exposes no config id in Inc 1 — seed on formID alone for now; mix the
        // config id in once the eval context/instance carries it (Inc 2/3), so two configs on one actor
        // roll independently (matches OAR's per-(actor, submod) roll).
        Value Fn_Random(const EvalContext& c, std::span<const Value>) {
            std::uint64_t seed = c.actor ? c.actor->GetFormID() : 0u;
            // splitmix64 finalizer — good avalanche for a small integer seed.
            seed += 0x9E3779B97F4A7C15ull;
            seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ull;
            seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBull;
            seed ^= seed >> 31;
            // top 24 bits → [0,1)
            return Value::Float(static_cast<float>(seed >> 40) / static_cast<float>(1u << 24));
        }

        // ═══════════════════════════════════════════════════════════════════════════════════════════
        //  FUNCTIONS — boolean predicates (pure over primitives)
        // ═══════════════════════════════════════════════════════════════════════════════════════════

        Value Fn_HasKeyword(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("Keywords");
            return Value::Bool(!a.empty() && a[0].type == VType::Keyword && KeywordArrayHas(c, p, a[0].kw));
        }

        Value Fn_IsWornHasKeyword(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("WornKeywords");
            return Value::Bool(!a.empty() && a[0].type == VType::Keyword && KeywordArrayHas(c, p, a[0].kw));
        }

        Value Fn_IsEquipped(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("EquippedForm");
            if (a.size() < 2 || !a[0].form) return Value::Bool(false);
            const std::array<Value, 1> args{ a[1] };
            return Value::Bool(c.Primitive(p, args).form == a[0].form);
        }

        Value Fn_IsEquippedType(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("EquippedType");
            if (a.size() < 2) return Value::Bool(false);
            const std::array<Value, 1> args{ a[1] };
            return Value::Bool(c.Primitive(p, args).AsInt() == a[0].AsInt());
        }

        Value Fn_IsEquippedHasKeyword(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("EquippedForm");
            if (a.size() < 2 || a[0].type != VType::Keyword || !a[0].kw) return Value::Bool(false);
            const std::array<Value, 1> args{ a[1] };
            RE::TESForm* form = c.Primitive(p, args).form;   // actor-state read via the memoized primitive
            if (!form) return Value::Bool(false);
            auto* kwf = form->As<RE::BGSKeywordForm>();       // pure form-data inspection
            return Value::Bool(kwf && kwf->HasKeyword(a[0].kw));
        }

        Value Fn_IsActorBase(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("ActorBase");
            return Value::Bool(!a.empty() && a[0].form && c.Primitive(p).form == a[0].form);
        }

        Value Fn_IsRace(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("Race");
            return Value::Bool(!a.empty() && a[0].form && c.Primitive(p).form == a[0].form);
        }

        Value Fn_IsInFaction(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("Factions");
            return Value::Bool(!a.empty() && FormArrayHas(c, p, a[0].form));
        }

        Value Fn_HasPerk(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("HasPerk");
            if (a.empty()) return Value::Bool(false);
            const std::array<Value, 1> args{ a[0] };
            return Value::Bool(c.Primitive(p, args).AsInt() != 0);
        }

        Value Fn_HasSpell(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("Spells");
            return Value::Bool(!a.empty() && FormArrayHas(c, p, a[0].form));
        }

        Value Fn_HasMagicEffect(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("MagicEffects");
            return Value::Bool(!a.empty() && FormArrayHas(c, p, a[0].form));
        }

        Value Fn_HasMagicEffectWithKeyword(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("MagicEffectKeywords");
            return Value::Bool(!a.empty() && a[0].type == VType::Keyword && KeywordArrayHas(c, p, a[0].kw));
        }

        Value Fn_IsFemale(const EvalContext& c, std::span<const Value>) {
            static const PrimId p = LookupPrimitive("Sex");
            return Value::Bool(c.Primitive(p).AsInt() == 1);
        }

        Value Fn_IsChild(const EvalContext& c, std::span<const Value>) {
            static const PrimId p = LookupPrimitive("Child");
            return Value::Bool(c.Primitive(p).AsInt() != 0);
        }

        Value Fn_IsUnique(const EvalContext& c, std::span<const Value>) {
            static const PrimId p = LookupPrimitive("Unique");
            return Value::Bool(c.Primitive(p).AsInt() != 0);
        }

        Value Fn_IsPlayerTeammate(const EvalContext& c, std::span<const Value>) {
            static const PrimId p = LookupPrimitive("PlayerTeammate");
            return Value::Bool(c.Primitive(p).AsInt() != 0);
        }

        // Movement/pose predicates — all one bit-test over the single memoized MoveFlags primitive.
        Value Fn_IsSneaking   (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_Sneaking); }
        Value Fn_IsRunning    (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_Running); }
        Value Fn_IsSprinting  (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_Sprinting); }
        Value Fn_IsSwimming   (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_Swimming); }
        Value Fn_IsInAir      (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_InAir); }
        Value Fn_IsMounted    (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_Mounted); }
        Value Fn_IsSitting    (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_Sitting); }
        Value Fn_IsSleeping   (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_Sleeping); }
        Value Fn_IsBleedingOut(const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_BleedingOut); }
        Value Fn_IsFlying     (const EvalContext& c, std::span<const Value>) { return Value::Bool(MoveFlagsOf(c) & kMB_Flying); }

        Value Fn_IsInCombat(const EvalContext& c, std::span<const Value>) {
            static const PrimId p = LookupPrimitive("CombatState");
            return Value::Bool(c.Primitive(p).AsInt() != 0);
        }

        Value Fn_IsInInterior(const EvalContext& c, std::span<const Value>) {
            static const PrimId p = LookupPrimitive("Interior");
            return Value::Bool(c.Primitive(p).AsInt() != 0);
        }

        Value Fn_IsCurrentWeather(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("Weather");
            return Value::Bool(!a.empty() && a[0].form && c.Primitive(p).form == a[0].form);
        }

        // CurrentWeatherHasKeyword(kw) -> Bool
        // Registered for roster/expression completeness, but the engine's RE::TESWeather carries NO
        // keyword form — there is no weather-keyword data to test — so this is always false. (Weather
        // "type" is TESWeather::Data classification, not keywords.) The converter should not emit it.
        Value Fn_CurrentWeatherHasKeyword(const EvalContext&, std::span<const Value>) {
            return Value::Bool(false);
        }

        // IsInLocation(form) -> Bool   (current location IS the target, or a descendant of it via parentLoc)
        Value Fn_IsInLocation(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("Location");
            if (a.empty() || !a[0].form) return Value::Bool(false);
            RE::TESForm* curForm = c.Primitive(p).form;      // actor-state read via the memoized primitive
            auto* target = a[0].form->As<RE::BGSLocation>();
            auto* cur = curForm ? curForm->As<RE::BGSLocation>() : nullptr;
            for (; cur; cur = cur->parentLoc)                // walk the static parent chain (pure form data)
                if (cur == target) return Value::Bool(true);
            return Value::Bool(false);
        }

        Value Fn_LocationHasKeyword(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("LocationKeywords");
            return Value::Bool(!a.empty() && a[0].type == VType::Keyword && KeywordArrayHas(c, p, a[0].kw));
        }

    }  // namespace

    void RegisterBuiltins() {
        using V = VType;

        // ── Param type descriptors (static: the registry borrows the span) ──────────────────────────
        static constexpr std::array kP_Int      { V::Int };
        static constexpr std::array kP_Form      { V::Form };
        static constexpr std::array kP_Keyword   { V::Keyword };
        static constexpr std::array kP_FormHand  { V::Form, V::Int };   // (form, hand)
        static constexpr std::array kP_IntHand   { V::Int,  V::Int };   // (type, hand)
        static constexpr std::array kP_KwHand    { V::Keyword, V::Int };// (keyword, hand)

        // ── PRIMITIVES ──────────────────────────────────────────────────────────────────────────────
        RegisterPrimitive("Keywords",            V::KeywordArray, {},         &Prim_Keywords);
        RegisterPrimitive("WornKeywords",        V::KeywordArray, {},         &Prim_WornKeywords);
        RegisterPrimitive("EquippedForm",        V::Form,         kP_Int,     &Prim_EquippedForm);
        RegisterPrimitive("EquippedType",        V::Int,          kP_Int,     &Prim_EquippedType);
        RegisterPrimitive("ActorValue",          V::Float,        kP_Int,     &Prim_ActorValue);
        RegisterPrimitive("ActorBase",           V::Form,         {},         &Prim_ActorBase);
        RegisterPrimitive("Race",                V::Form,         {},         &Prim_Race);
        RegisterPrimitive("Factions",            V::FormArray,    {},         &Prim_Factions);
        RegisterPrimitive("Perks",               V::FormArray,    {},         &Prim_Perks);
        RegisterPrimitive("HasPerk",             V::Int,          kP_Form,    &Prim_HasPerk);   // supports HasPerk()
        RegisterPrimitive("Spells",              V::FormArray,    {},         &Prim_Spells);
        RegisterPrimitive("MagicEffects",        V::FormArray,    {},         &Prim_MagicEffects);
        RegisterPrimitive("MagicEffectKeywords", V::KeywordArray, {},         &Prim_MagicEffectKeywords);
        RegisterPrimitive("Level",               V::Int,          {},         &Prim_Level);
        RegisterPrimitive("Sex",                 V::Int,          {},         &Prim_Sex);
        RegisterPrimitive("MoveFlags",           V::Int,          {},         &Prim_MoveFlags);
        RegisterPrimitive("Weather",             V::Form,         {},         &Prim_Weather);
        RegisterPrimitive("GameTime",            V::Float,        {},         &Prim_GameTime);
        RegisterPrimitive("Mount",               V::Form,         {},         &Prim_Mount);
        RegisterPrimitive("CombatState",         V::Int,          {},         &Prim_CombatState);
        RegisterPrimitive("GraphVar",            V::Float,        kP_Keyword, &Prim_GraphVar);
        RegisterPrimitive("Global",              V::Float,        kP_Form,    &Prim_Global);
        RegisterPrimitive("FactionRank",         V::Int,          kP_Form,    &Prim_FactionRank); // supports FactionRank()
        RegisterPrimitive("Location",            V::Form,         {},         &Prim_Location);
        RegisterPrimitive("LocationKeywords",    V::KeywordArray, {},         &Prim_LocationKeywords);
        RegisterPrimitive("Interior",            V::Int,          {},         &Prim_Interior);        // supports IsInInterior()
        RegisterPrimitive("Child",               V::Int,          {},         &Prim_Child);           // supports IsChild()
        RegisterPrimitive("Unique",              V::Int,          {},         &Prim_Unique);          // supports IsUnique()
        RegisterPrimitive("PlayerTeammate",      V::Int,          {},         &Prim_PlayerTeammate);  // supports IsPlayerTeammate()

        // ── FUNCTIONS — value-returning ──────────────────────────────────────────────────────────────
        RegisterFunction("GetAV",       V::Float, kP_Int,   &Fn_GetAV);
        RegisterFunction("Level",       V::Int,   {},       &Fn_Level);
        RegisterFunction("GameTime",    V::Float, {},       &Fn_GameTime);
        RegisterFunction("FactionRank", V::Int,   kP_Form,  &Fn_FactionRank);
        RegisterFunction("GraphVar",    V::Float, kP_Keyword, &Fn_GraphVar);
        RegisterFunction("Global",      V::Float, kP_Form,  &Fn_Global);
        RegisterFunction("Random",      V::Float, {},       &Fn_Random);

        // ── FUNCTIONS — boolean predicates ───────────────────────────────────────────────────────────
        RegisterFunction("HasKeyword",               V::Bool, kP_Keyword,  &Fn_HasKeyword);
        RegisterFunction("IsWornHasKeyword",         V::Bool, kP_Keyword,  &Fn_IsWornHasKeyword);
        RegisterFunction("IsEquipped",               V::Bool, kP_FormHand, &Fn_IsEquipped);
        RegisterFunction("IsEquippedType",           V::Bool, kP_IntHand,  &Fn_IsEquippedType);
        RegisterFunction("IsEquippedHasKeyword",     V::Bool, kP_KwHand,   &Fn_IsEquippedHasKeyword);
        RegisterFunction("IsActorBase",              V::Bool, kP_Form,     &Fn_IsActorBase);
        RegisterFunction("IsRace",                   V::Bool, kP_Form,     &Fn_IsRace);
        RegisterFunction("IsInFaction",              V::Bool, kP_Form,     &Fn_IsInFaction);
        RegisterFunction("HasPerk",                  V::Bool, kP_Form,     &Fn_HasPerk);
        RegisterFunction("HasSpell",                 V::Bool, kP_Form,     &Fn_HasSpell);
        RegisterFunction("HasMagicEffect",           V::Bool, kP_Form,     &Fn_HasMagicEffect);
        RegisterFunction("HasMagicEffectWithKeyword",V::Bool, kP_Keyword,  &Fn_HasMagicEffectWithKeyword);
        RegisterFunction("IsFemale",                 V::Bool, {},          &Fn_IsFemale);
        RegisterFunction("IsChild",                  V::Bool, {},          &Fn_IsChild);
        RegisterFunction("IsUnique",                 V::Bool, {},          &Fn_IsUnique);
        RegisterFunction("IsPlayerTeammate",         V::Bool, {},          &Fn_IsPlayerTeammate);
        RegisterFunction("IsSneaking",               V::Bool, {},          &Fn_IsSneaking);
        RegisterFunction("IsRunning",                V::Bool, {},          &Fn_IsRunning);
        RegisterFunction("IsSprinting",              V::Bool, {},          &Fn_IsSprinting);
        RegisterFunction("IsSwimming",               V::Bool, {},          &Fn_IsSwimming);
        RegisterFunction("IsInAir",                  V::Bool, {},          &Fn_IsInAir);
        RegisterFunction("IsMounted",                V::Bool, {},          &Fn_IsMounted);
        RegisterFunction("IsSitting",                V::Bool, {},          &Fn_IsSitting);
        RegisterFunction("IsSleeping",               V::Bool, {},          &Fn_IsSleeping);
        RegisterFunction("IsBleedingOut",            V::Bool, {},          &Fn_IsBleedingOut);
        RegisterFunction("IsFlying",                 V::Bool, {},          &Fn_IsFlying);
        RegisterFunction("IsInCombat",               V::Bool, {},          &Fn_IsInCombat);
        RegisterFunction("IsInInterior",             V::Bool, {},          &Fn_IsInInterior);
        RegisterFunction("IsCurrentWeather",         V::Bool, kP_Form,     &Fn_IsCurrentWeather);
        RegisterFunction("CurrentWeatherHasKeyword", V::Bool, kP_Keyword,  &Fn_CurrentWeatherHasKeyword);
        RegisterFunction("IsInLocation",             V::Bool, kP_Form,     &Fn_IsInLocation);
        RegisterFunction("LocationHasKeyword",       V::Bool, kP_Keyword,  &Fn_LocationHasKeyword);
    }

}  // namespace CB::conditions
