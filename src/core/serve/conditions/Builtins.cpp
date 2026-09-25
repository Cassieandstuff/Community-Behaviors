// Builtins.cpp — the BUILT-IN primitive/function roster.
//
// ┌─────────────────────────────────────────────────────────────────────────────────────────────┐
// │ SEAM: this file is owned by the roster branch. It holds ONE worked example (Keywords primitive │
// │ + HasKeyword function) as the template. Fill out the full roster here (see the spec §5a):      │
// │   PRIMITIVES: Keywords, WornKeywords, EquippedForm/Type, ActorValue, ActorBase, Race,          │
// │               Factions, Perks, Spells, MagicEffects, Level, Sex, MoveFlags, Weather, GameTime, │
// │               Mount, CombatState, GraphVar, Global, Location, LocationKeywords.                │
// │   FUNCTIONS (value): GetAV, Level, GameTime, FactionRank, GraphVar, Global, Random.            │
// │   FUNCTIONS (bool):  HasKeyword, IsWornHasKeyword, IsEquipped(Type/HasKeyword), IsActorBase,   │
// │               IsRace, IsInFaction, HasPerk, HasSpell, HasMagicEffect(WithKeyword), IsFemale,   │
// │               IsChild, IsUnique, IsPlayerTeammate, IsSneaking/…/IsFlying, IsInCombat,          │
// │               IsInInterior, IsCurrentWeather, CurrentWeatherHasKeyword, IsInLocation,          │
// │               LocationHasKeyword.                                                              │
// │ Random() must seed hash(actor->formID, configId) — stable per (actor,config) (spec §5a).       │
// └─────────────────────────────────────────────────────────────────────────────────────────────┘
#include "Conditions.h"

#include <array>

namespace CB::conditions {

    namespace {
        // EXAMPLE PRIMITIVE — the actor's base keywords as an array (memoized per pass).
        // NOTE: illustrative; the roster branch verifies the exact CommonLib accessors.
        Value Prim_Keywords(const EvalContext& c, std::span<const Value>) {
            auto& block = c.ArenaBlock();
            if (auto* actor = c.actor) {
                if (auto* npc = actor->GetActorBase()) {
                    const std::uint32_t n = npc->numKeywords;
                    for (std::uint32_t k = 0; k < n; ++k)
                        if (npc->keywords && npc->keywords[k])
                            block.push_back(Value::Keyword(npc->keywords[k]));
                }
            }
            return Value::Array(VType::KeywordArray, block);
        }

        // EXAMPLE FUNCTION — HasKeyword(keyword) -> Bool, built on the Keywords primitive.
        Value Fn_HasKeyword(const EvalContext& c, std::span<const Value> a) {
            static const PrimId kKeywords = LookupPrimitive("Keywords");
            const Value kws = c.Primitive(kKeywords);
            if (a.empty() || a[0].type != VType::Keyword) return Value::Bool(false);
            for (const Value& k : kws.arr)
                if (k.kw == a[0].kw) return Value::Bool(true);
            return Value::Bool(false);
        }
    }  // namespace

    void RegisterBuiltins() {
        RegisterPrimitive("Keywords", VType::KeywordArray, {}, &Prim_Keywords);

        static constexpr std::array kHasKeywordParams{ VType::Keyword };
        RegisterFunction("HasKeyword", VType::Bool, kHasKeywordParams, &Fn_HasKeyword);

        // roster branch: add the rest here.
    }

}  // namespace CB::conditions
