#include "PCH.h"

#include "CharMoveRules.h"

#include "CharComponents.h"
#include "CharSpeedProfile.h"

#include <config/Config.h>
#include <core/debug/Settings.h>
#include <hky/relay/DynamicClip.h>
#include <platform/startup/Startup.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace CB::feature::controller::rules {

    namespace {

        namespace loco = hky::locomotion;
        using hky::relay::Value;
        using hky::relay::VType;

        constexpr const char* kRulesDir = "Data/SKSE/Plugins/Community Behaviors/Movement";
        constexpr double      kStale    = 0.5;

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        bool On()
        {
            static const bool on = core::debug::FlagOn("char_move_rules");
            return on;
        }

        bool SameValue(const Value& a, const Value& b)
        {
            if (a.type != b.type) return false;
            switch (a.type) {
                case VType::Bool: return a.b == b.b;
                case VType::Int: return a.i == b.i;
                case VType::Float: return a.f == b.f;
                default: return false;
            }
        }

        /// @brief One driven actor's rules: its smoother, the latest result, and the graph variables they hold.
        struct Entry {
            loco::ModifierSmoother           smoother;
            ModifierState                    state;
            std::vector<std::string>         active;
            double                           at = -1.0e9;
            std::map<std::string, Value>     applied;     ///< what the rules set now, by name byte-exact
            std::map<std::string, Value>     originals;   ///< each variable's value before a rule first set it
        };

        struct Table {
            std::mutex                                 lock;
            std::shared_ptr<const loco::RuleSet>       rules;
            std::unordered_map<RE::FormID, Entry>      byActor;
            std::atomic<int>                           gaitCap{ static_cast<int>(Gait::Sprint) };   ///< the player's
        };
        Table& T()
        {
            static Table t;
            return t;
        }

        std::shared_ptr<const loco::RuleSet> Load()
        {
            std::vector<loco::RuleDocument> docs;
            for (const CB::config::Source& src : CB::config::Discover({ { kRulesDir }, { ".json" } })) {
                if (auto text = CB::config::ReadText(src.path)) docs.push_back({ src.key, std::move(*text) });
                else LOG_WARN("movement rules: {} could not be read", src.key);
            }
            loco::RuleLoadOptions options;
            /// @note A modded material named by its MATT record's FormKey (or EditorID): the record's material id.
            options.resolveMaterial = [](std::string_view text) -> std::optional<std::uint32_t> {
                RE::TESForm* form = nullptr;
                if (const auto key = hky::relay::ParseFormKey(text)) {
                    if (auto* dh = RE::TESDataHandler::GetSingleton()) form = dh->LookupForm(key->id, key->plugin);
                } else {
                    form = RE::TESForm::LookupByEditorID(text);
                }
                const auto* material = form ? form->As<RE::BGSMaterialType>() : nullptr;
                if (!material) return std::nullopt;
                return static_cast<std::uint32_t>(material->materialID);
            };
            auto set = std::make_shared<loco::RuleSet>(loco::LoadRules(docs, options));
            for (const std::string& e : set->errors) LOG_WARN("movement rules: {}", e);
            int live = 0;
            for (const loco::Rule& r : set->rules) {
                if (!r.error.empty()) LOG_WARN("movement rules: '{}' ({}) never applies: {}", r.id, r.sources.back(), r.error);
                else if (r.Live()) ++live;
            }
            LOG_INFO("movement rules: {} rules from {} files ({} live), {} material groups", set->rules.size(), docs.size(), live,
                     set->groups->size());
            hky::relay::SetMaterialGroups(set->groups);
            return set;
        }

        void LoadAtStartup()
        {
            if (!On()) return;
            auto set = Load();
            std::scoped_lock guard(T().lock);
            T().rules = std::move(set);
        }

        [[maybe_unused]] const bool s_loaded = platform::startup::OnDataLoaded(&LoadAtStartup, "movement rules");

        /// @brief Writes or restores graph variables on the main thread: the frame driver runs on job threads.
        struct VarOp {
            std::string name;
            Value       value;
            bool        restore = false;
        };

        bool ReadVar(RE::Actor& actor, const std::string& name, VType type, Value& out)
        {
            switch (type) {
                case VType::Bool: {
                    bool b = false;
                    if (!actor.GetGraphVariableBool(name.c_str(), b)) return false;
                    out = Value::Bool(b);
                    return true;
                }
                case VType::Int: {
                    std::int32_t i = 0;
                    if (!actor.GetGraphVariableInt(name.c_str(), i)) return false;
                    out = Value::Int(i);
                    return true;
                }
                case VType::Float: {
                    float f = 0.0f;
                    if (!actor.GetGraphVariableFloat(name.c_str(), f)) return false;
                    out = Value::Float(f);
                    return true;
                }
                default: return false;
            }
        }

        void WriteVar(RE::Actor& actor, const std::string& name, const Value& v)
        {
            switch (v.type) {
                case VType::Bool: (void)actor.SetGraphVariableBool(name.c_str(), v.b); break;
                case VType::Int: (void)actor.SetGraphVariableInt(name.c_str(), v.i); break;
                case VType::Float: (void)actor.SetGraphVariableFloat(name.c_str(), v.f); break;
                default: break;
            }
        }

        void RunVarOps(RE::ActorHandle handle, RE::FormID id, std::vector<VarOp> ops)
        {
            const RE::NiPointer<RE::Actor> actor = handle.get();
            if (!actor) return;
            Table&           t = T();
            std::scoped_lock guard(t.lock);
            Entry&           e = t.byActor[id];
            for (const VarOp& op : ops) {
                if (op.restore) {
                    if (const auto it = e.originals.find(op.name); it != e.originals.end()) {
                        WriteVar(*actor, op.name, it->second);
                        e.originals.erase(it);
                    }
                    continue;
                }
                if (!e.originals.contains(op.name)) {
                    Value before;
                    if (ReadVar(*actor, op.name, op.value.type, before)) e.originals.emplace(op.name, before);
                }
                WriteVar(*actor, op.name, op.value);
            }
        }

        bool Active(RE::Actor& actor)
        {
            if (!On()) return false;
            Table&           t = T();
            std::scoped_lock guard(t.lock);
            const auto       it = t.byActor.find(actor.GetFormID());
            return it != t.byActor.end() && Now() - it->second.at <= kStale && !it->second.smoother.Idle();
        }

        void Apply(RE::Actor& actor, component::Frame& f)
        {
            ModifierState state;
            {
                Table&           t = T();
                std::scoped_lock guard(t.lock);
                const auto       it = t.byActor.find(actor.GetFormID());
                if (it == t.byActor.end()) return;
                state = it->second.state;
            }
            loco::ApplyModifiers(state, f.profile, f.caps);
            for (const auto& [tag, w] : state.tags) f.AddTag(tag, w);
        }

        /// @note After Swim: water reshapes the profile first, the rules scale what water left.
        [[maybe_unused]] const bool s_registered = component::Register({ "Rules", 100, &Active, &Apply });

    }  // namespace

    ModifierState Step(RE::Actor& actor, float dt)
    {
        if (!On()) return ModifierState{};
        Table&                               t = T();
        std::shared_ptr<const loco::RuleSet> rules;
        {
            std::scoped_lock guard(t.lock);
            rules = t.rules;
        }
        if (!rules) return ModifierState{};

        hky::relay::EvalContext ctx;
        ctx.Reset(&actor);
        const loco::ModifierGoals goals = loco::Evaluate(*rules, ctx);

        std::vector<VarOp> ops;
        ModifierState      out;
        {
            std::scoped_lock guard(t.lock);
            Entry&           e = t.byActor[actor.GetFormID()];
            e.smoother.Step(goals, dt);
            e.state = e.smoother.State();
            e.at    = Now();
            e.active.assign(goals.active.begin(), goals.active.end());

            std::map<std::string, Value> wanted;
            for (const auto& [name, v] : goals.graphVars) wanted.emplace(std::string(name), v);
            for (const auto& [name, v] : wanted) {
                const auto it = e.applied.find(name);
                if (it == e.applied.end() || !SameValue(it->second, v)) ops.push_back({ name, v, false });
            }
            for (const auto& [name, v] : e.applied)
                if (!wanted.contains(name)) ops.push_back({ name, v, true });
            e.applied = std::move(wanted);
            out       = e.state;
            if (actor.IsPlayerRef()) t.gaitCap.store(static_cast<int>(out.maxGait), std::memory_order_relaxed);
        }
        if (!ops.empty()) {
            if (auto* tasks = SKSE::GetTaskInterface())
                tasks->AddTask([handle = actor.GetHandle(), id = actor.GetFormID(), ops = std::move(ops)]() mutable {
                    RunVarOps(handle, id, std::move(ops));
                });
        }
        return out;
    }

    Gait GaitCap(RE::FormID actor)
    {
        if (actor != move::kPlayerId || !On()) return Gait::Sprint;
        return static_cast<Gait>(T().gaitCap.load(std::memory_order_relaxed));
    }

    Gait CapGait(RE::Actor& actor, const ModifierState& m, Gait gait, float& full)
    {
        const Gait capped = loco::CapGait(gait, m);
        if (capped == Gait::Walk && gait == Gait::Run) full = speed::WalkOfRun(&actor, full);
        return capped;
    }

    bool Current(Hud& out)
    {
        if (!On()) return false;
        Table&           t = T();
        std::scoped_lock guard(t.lock);
        const auto       it = t.byActor.find(move::kPlayerId);
        if (!t.rules || it == t.byActor.end() || Now() - it->second.at > kStale) return false;
        out.loaded = static_cast<int>(t.rules->rules.size());
        out.active = it->second.active;
        out.state  = it->second.state;
        out.errors.clear();
        for (const std::string& e : t.rules->errors)
            if (out.errors.size() < 4) out.errors.push_back(e);
        for (const loco::Rule& r : t.rules->rules)
            if (!r.error.empty() && out.errors.size() < 4) out.errors.push_back(r.id + ": " + r.error);
        return true;
    }

    void Reload()
    {
        if (!On()) return;
        auto             set = Load();
        std::scoped_lock guard(T().lock);
        T().rules = std::move(set);
    }

}  // namespace CB::feature::controller::rules
