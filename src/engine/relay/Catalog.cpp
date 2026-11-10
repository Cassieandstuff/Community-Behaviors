#include "Relay.h"

#include <algorithm>
#include <array>
#include <format>

/// @file
/// @brief Every function the grammar can call, declared once: the plugin binds the game's implementations to these names
///        (platform/relay/Builtins.cpp), the offline tools bind GraphVar (CoreBuiltins.cpp), and an editor lists them all.
/// @note  CommonLib-free on purpose: an editor reads the game's vocabulary without linking the game.

namespace CB::engine::relay {

    namespace {

        using V = VType;

        // ── Choice tables ──────────────────────────────────────────────────────────────────────────
        /// @note Hand: 1 is the left hand, anything else the right (GetEquippedObject's bool leftHand; OAR's "Left hand").
        constexpr std::array kHand{ Choice{ "Right", 0 }, Choice{ "Left", 1 } };

        /// @note RE::WEAPON_TYPE, plus what EquippedType reports for an equipped item that is not a weapon. An empty hand
        ///       reads as HandToHandMelee.
        constexpr std::array kWeaponType{
            Choice{ "NotAWeapon", -1 },  Choice{ "HandToHandMelee", 0 }, Choice{ "OneHandSword", 1 }, Choice{ "OneHandDagger", 2 },
            Choice{ "OneHandAxe", 3 },   Choice{ "OneHandMace", 4 },     Choice{ "TwoHandSword", 5 }, Choice{ "TwoHandAxe", 6 },
            Choice{ "Bow", 7 },          Choice{ "Staff", 8 },           Choice{ "Crossbow", 9 },
        };

        // ── Parameter lists (static: a CallableInfo borrows them) ──────────────────────────────────
        constexpr std::array kP_Variable{ ParamInfo{ "variable", V::String, {} } };
        constexpr std::array kP_ActorValue{ ParamInfo{ "actorValue", V::Int, {} } };
        constexpr std::array kP_Faction{ ParamInfo{ "faction", V::Form, {} } };
        constexpr std::array kP_Global{ ParamInfo{ "global", V::Form, {} } };
        constexpr std::array kP_Keyword{ ParamInfo{ "keyword", V::Keyword, {} } };
        constexpr std::array kP_FormHand{ ParamInfo{ "form", V::Form, {} }, ParamInfo{ "hand", V::Int, "Hand" } };
        constexpr std::array kP_TypeHand{ ParamInfo{ "type", V::Int, "WEAPON_TYPE" }, ParamInfo{ "hand", V::Int, "Hand" } };
        constexpr std::array kP_KeywordHand{ ParamInfo{ "keyword", V::Keyword, {} }, ParamInfo{ "hand", V::Int, "Hand" } };
        constexpr std::array kP_ActorBase{ ParamInfo{ "actorBase", V::Form, {} } };
        constexpr std::array kP_Race{ ParamInfo{ "race", V::Form, {} } };
        constexpr std::array kP_Perk{ ParamInfo{ "perk", V::Form, {} } };
        constexpr std::array kP_Spell{ ParamInfo{ "spell", V::Form, {} } };
        constexpr std::array kP_Effect{ ParamInfo{ "magicEffect", V::Form, {} } };
        constexpr std::array kP_Weather{ ParamInfo{ "weather", V::Form, {} } };
        constexpr std::array kP_Location{ ParamInfo{ "location", V::Form, {} } };

        constexpr std::span<const ParamInfo> kNone{};

        const std::array kCallables{
            // Graph
            CallableInfo{ "GraphVar", V::Float, kP_Variable, "Graph",
                          "The value of the graph variable of that exact name, typed as the graph declares it; 0 when there is none." },

            // Values
            CallableInfo{ "GetAV", V::Float, kP_ActorValue, "Values", "The actor's current value of an actor value (RE::ActorValue index)." },
            CallableInfo{ "Level", V::Int, kNone, "Values", "The actor's level." },
            CallableInfo{ "GameTime", V::Float, kNone, "Values", "The hour of the game day, 0 to 24." },
            CallableInfo{ "FactionRank", V::Int, kP_Faction, "Values", "The actor's rank in the faction; -2 when not a member." },
            CallableInfo{ "Global", V::Float, kP_Global, "Values", "The value of a global variable." },
            CallableInfo{ "Random", V::Float, kNone, "Values", "A number in [0, 1), the same for this actor all session." },

            // Actor
            CallableInfo{ "IsFemale", V::Bool, kNone, "Actor", "The actor is female." },
            CallableInfo{ "IsChild", V::Bool, kNone, "Actor", "The actor is a child." },
            CallableInfo{ "IsUnique", V::Bool, kNone, "Actor", "The actor's base is unique." },
            CallableInfo{ "IsPlayerTeammate", V::Bool, kNone, "Actor", "The actor is the player's teammate." },
            CallableInfo{ "IsInCombat", V::Bool, kNone, "Actor", "The actor is in combat." },
            CallableInfo{ "HasKeyword", V::Bool, kP_Keyword, "Actor", "The actor's base or template has the keyword." },
            CallableInfo{ "IsActorBase", V::Bool, kP_ActorBase, "Actor", "The actor's base is that one." },
            CallableInfo{ "IsRace", V::Bool, kP_Race, "Actor", "The actor is of that race." },
            CallableInfo{ "IsInFaction", V::Bool, kP_Faction, "Actor", "The actor is a member of the faction." },
            CallableInfo{ "HasPerk", V::Bool, kP_Perk, "Actor", "The actor has the perk." },
            CallableInfo{ "HasSpell", V::Bool, kP_Spell, "Actor", "The actor knows the spell." },
            CallableInfo{ "HasMagicEffect", V::Bool, kP_Effect, "Actor", "A magic effect of that kind is active on the actor." },
            CallableInfo{ "HasMagicEffectWithKeyword", V::Bool, kP_Keyword, "Actor", "An active magic effect on the actor has the keyword." },

            // Movement
            CallableInfo{ "IsSneaking", V::Bool, kNone, "Movement", "The actor is sneaking." },
            CallableInfo{ "IsRunning", V::Bool, kNone, "Movement", "The actor is running." },
            CallableInfo{ "IsSprinting", V::Bool, kNone, "Movement", "The actor is sprinting." },
            CallableInfo{ "IsSwimming", V::Bool, kNone, "Movement", "The actor is swimming." },
            CallableInfo{ "IsInAir", V::Bool, kNone, "Movement", "The actor is in midair." },
            CallableInfo{ "IsMounted", V::Bool, kNone, "Movement", "The actor is on a mount." },
            CallableInfo{ "IsSitting", V::Bool, kNone, "Movement", "The actor is sitting." },
            CallableInfo{ "IsSleeping", V::Bool, kNone, "Movement", "The actor is sleeping." },
            CallableInfo{ "IsBleedingOut", V::Bool, kNone, "Movement", "The actor is bleeding out." },
            CallableInfo{ "IsFlying", V::Bool, kNone, "Movement", "The actor is flying." },

            // Equipment
            CallableInfo{ "IsEquipped", V::Bool, kP_FormHand, "Equipment", "The hand holds that item." },
            CallableInfo{ "IsEquippedType", V::Bool, kP_TypeHand, "Equipment", "The hand holds a weapon of that type; an empty hand counts as HandToHandMelee." },
            CallableInfo{ "IsEquippedHasKeyword", V::Bool, kP_KeywordHand, "Equipment", "The item in the hand has the keyword." },
            CallableInfo{ "IsWornHasKeyword", V::Bool, kP_Keyword, "Equipment", "Some worn armor piece has the keyword." },

            // World
            CallableInfo{ "IsInInterior", V::Bool, kNone, "World", "The actor is in an interior cell." },
            CallableInfo{ "IsCurrentWeather", V::Bool, kP_Weather, "World", "The current weather is that one." },
            CallableInfo{ "CurrentWeatherHasKeyword", V::Bool, kP_Keyword, "World",
                          "Always false: the game's weather carries no keywords. Kept so converted configs compile." },
            CallableInfo{ "IsInLocation", V::Bool, kP_Location, "World", "The actor's location is that one or lies inside it." },
            CallableInfo{ "LocationHasKeyword", V::Bool, kP_Keyword, "World", "The actor's current location has the keyword." },
        };

    }  // namespace

    std::span<const CallableInfo> ListCallables() { return kCallables; }

    const CallableInfo* FindCallable(std::string_view name) {
        const auto it = std::ranges::find(kCallables, name, &CallableInfo::name);
        return it == kCallables.end() ? nullptr : &*it;
    }

    std::span<const Choice> Choices(std::string_view table) {
        if (table == "Hand") return kHand;
        if (table == "WEAPON_TYPE") return kWeaponType;
        return {};
    }

    bool Bind(std::string_view name, FunctionFn fn, std::string& err) {
        const CallableInfo* declared = FindCallable(name);
        if (!declared) {
            err = std::format("'{}' is not in the relay catalog", name);
            return false;
        }
        if (LookupFunction(name) != FnId::Invalid) {
            err = std::format("'{}' is already bound", name);
            return false;
        }
        std::vector<VType> params;
        for (const ParamInfo& p : declared->params) params.push_back(p.type);
        RegisterFunction(declared->name, declared->returns, params, fn);
        return true;
    }

    std::vector<std::string_view> Unbound() {
        std::vector<std::string_view> out;
        for (const CallableInfo& c : kCallables)
            if (LookupFunction(c.name) == FnId::Invalid) out.push_back(c.name);
        return out;
    }

}  // namespace CB::engine::relay
