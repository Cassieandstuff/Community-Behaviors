#include "PCH.h"

#include "CharSpeedProfile.h"

#include "core/debug/Settings.h"

#include <array>
#include <format>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace CB::feature::controller::speed {

    namespace {

        namespace loco = CB::engine::locomotion;
        using Dir      = RE::Movement::SPEED_DIRECTIONS;

        struct Entry {
            std::optional<loco::SpeedProfile> profile;
            const RE::BGSMovementType*        record = nullptr;   ///< the form, whose speeds are the record's as loaded
        };

        /// @note Names are kept byte-exact; the forms are found by the pooled name's data pointer, as the engine's own
        ///       lookup of a type by name goes through the same pool.
        struct Table {
            std::mutex                                                   lock;
            std::map<std::string, Entry>                                 byName;
            std::unordered_map<const char*, const RE::BGSMovementType*> forms;
            bool                                                         formsRead = false;
        };
        Table& Types_()
        {
            static Table t;
            return t;
        }

        const RE::BGSMovementType* RecordOf(Table& t, const std::string& name)
        {
            if (!t.formsRead) {
                auto* data = RE::TESDataHandler::GetSingleton();
                if (!data) return nullptr;
                for (auto* form : data->GetFormArray<RE::BGSMovementType>())
                    if (form && form->movementTypeData.typeName.data()) t.forms.emplace(form->movementTypeData.typeName.data(), form);
                t.formsRead = true;
            }
            const RE::BSFixedString pooled(name.c_str());
            const auto              it = t.forms.find(pooled.data());
            return it == t.forms.end() ? nullptr : it->second;
        }

        std::optional<loco::SpeedProfile> FromSettings(const std::string& type)
        {
            const std::string section = loco::SpeedSection(type);
            return loco::ResolveSpeedProfile(type, [&](std::string_view key) { return core::debug::Value(section, key); });
        }

        /// @pre t.lock held.
        Entry& EntryOf(Table& t, const std::string& type)
        {
            auto it = t.byName.find(type);
            if (it != t.byName.end()) return it->second;
            Entry e;
            e.profile = FromSettings(type);
            e.record  = RecordOf(t, type);
            if (e.profile) {
                const auto& p = *e.profile;
                LOG_INFO("speed profile '{}': walk {:.0f}/{:.0f}/{:.0f}, run {:.0f}/{:.0f}/{:.0f} (forward/side/back; settings.ini [{}] over "
                         "the built-ins){}",
                         type, p.walk.forward, p.walk.side, p.walk.back, p.run.forward, p.run.side, p.run.back, loco::SpeedSection(type),
                         e.record ? "" : " - no movement type record of that name, so the engine's speed stands");
            }
            return t.byName.emplace(type, std::move(e)).first->second;
        }

        /// @brief `actor`'s current movement type's profile and record; false when it has no profile or no record.
        bool Lookup(RE::Actor* actor, loco::SpeedProfile& profile, const RE::BGSMovementType*& record)
        {
            const std::string type = CurrentType(actor);
            if (type.empty()) return false;
            Table&           t = Types_();
            std::scoped_lock guard(t.lock);
            const Entry&     e = EntryOf(t, type);
            if (!e.profile || !e.record) return false;
            profile = *e.profile;
            record  = e.record;
            return true;
        }

        const loco::DirectionSpeeds& Column(const loco::SpeedProfile& p, Gait gait) { return gait == Gait::Walk ? p.walk : p.run; }
        int ColumnIndex(Gait gait) { return gait == Gait::Walk ? RE::Movement::MaxSpeeds::kWalk : RE::Movement::MaxSpeeds::kRun; }

        constexpr std::array<std::string_view, 8> kBuiltIns{ "NPCDefault", "NPC1HM", "NPC2HM", "NPCBow",
                                                             "NPCMagic", "NPCSneaking", "NPCSprinting", "AIControlledNPCSprinting" };

    }  // namespace

    std::string CurrentType(RE::Actor* actor)
    {
        auto* process = actor ? actor->GetActorRuntimeData().currentProcess : nullptr;
        auto* high    = process ? process->high : nullptr;
        if (!high) return {};
        const char* name = high->currentMovementType.typeName.c_str();
        return name ? std::string(name) : std::string();
    }

    float Forward(RE::Actor* actor, Gait gait, float engine)
    {
        loco::SpeedProfile         p;
        const RE::BGSMovementType* record = nullptr;
        if (!Lookup(actor, p, record)) return engine;
        const auto& stated = record->movementTypeData.defaultData.speeds[Dir::kForward];
        /// @note An AI walking raises neither the walk nor the run flag: its speed is mapped piecewise instead, the record's
        ///       walk to the profile's walk and its run to the profile's run. That loses the multipliers' exactness, which
        ///       the gait's own factor keeps.
        if (gait == Gait::None) {
            const float walkR = stated[RE::Movement::MaxSpeeds::kWalk], runR = stated[RE::Movement::MaxSpeeds::kRun];
            const float walkP = p.walk.forward > 0.0f ? p.walk.forward : walkR, runP = p.run.forward > 0.0f ? p.run.forward : runR;
            if (walkR <= 0.0f) return engine;
            if (engine <= walkR || runR <= walkR) return engine * walkP / walkR;
            return walkP + (engine - walkR) * (runP - walkP) / (runR - walkR);
        }
        return engine * loco::SpeedFactor(Column(p, gait).forward, stated[ColumnIndex(gait)]);
    }

    namespace {
        /// @brief The type's forward walk and run: the profile's, else the record's; false without a movement type.
        bool WalkAndRun(RE::Actor* actor, float& walk, float& run)
        {
            loco::SpeedProfile         p;
            const RE::BGSMovementType* record = nullptr;
            if (!Lookup(actor, p, record)) return false;
            const auto& stated = record->movementTypeData.defaultData.speeds[Dir::kForward];
            walk = p.walk.forward > 0.0f ? p.walk.forward : stated[RE::Movement::MaxSpeeds::kWalk];
            run  = p.run.forward > 0.0f ? p.run.forward : stated[RE::Movement::MaxSpeeds::kRun];
            return walk > 0.0f && run > walk;
        }
    }  // namespace

    float WalkOfRun(RE::Actor* actor, float run)
    {
        float walkF = 0.0f, runF = 0.0f;
        return WalkAndRun(actor, walkF, runF) ? run * walkF / runF : run;
    }

    Gait GaitOfSpeed(RE::Actor* actor, float speed)
    {
        float walkF = 0.0f, runF = 0.0f;
        if (!WalkAndRun(actor, walkF, runF)) return Gait::Run;
        return speed <= 0.5f * (walkF + runF) ? Gait::Walk : Gait::Run;
    }

    void Ratios(RE::Actor* actor, Gait gait, move::Caps& caps)
    {
        loco::SpeedProfile         p;
        const RE::BGSMovementType* record = nullptr;
        if (!Lookup(actor, p, record)) return;
        const loco::DirectionSpeeds& c = Column(p, gait);
        if (c.forward <= 0.0f) return;
        if (c.side > 0.0f) caps.left = caps.right = c.side / c.forward;
        if (c.back > 0.0f) caps.back = c.back / c.forward;
    }

    std::vector<TypeView> Types()
    {
        Table&           t = Types_();
        std::scoped_lock guard(t.lock);
        for (const auto name : kBuiltIns) EntryOf(t, std::string(name));
        std::vector<TypeView> out;
        const auto            view = [&](const std::string& name, const Entry& e) {
            TypeView v;
            v.name    = name;
            v.profile = e.profile;
            v.builtIn = loco::DefaultSpeedProfile(name).has_value();
            v.record  = e.record != nullptr;
            if (e.record) {
                const auto& sp = e.record->movementTypeData.defaultData.speeds;
                v.stated.walk  = { sp[Dir::kForward][RE::Movement::MaxSpeeds::kWalk], sp[Dir::kLeft][RE::Movement::MaxSpeeds::kWalk],
                                   sp[Dir::kBack][RE::Movement::MaxSpeeds::kWalk] };
                v.stated.run   = { sp[Dir::kForward][RE::Movement::MaxSpeeds::kRun], sp[Dir::kLeft][RE::Movement::MaxSpeeds::kRun],
                                   sp[Dir::kBack][RE::Movement::MaxSpeeds::kRun] };
            }
            return v;
        };
        for (const auto name : kBuiltIns) out.push_back(view(std::string(name), t.byName.at(std::string(name))));
        for (const auto& [name, e] : t.byName)
            if (!loco::DefaultSpeedProfile(name)) out.push_back(view(name, e));
        return out;
    }

    void Set(const std::string& type, const loco::SpeedProfile& profile)
    {
        Table&           t = Types_();
        std::scoped_lock guard(t.lock);
        EntryOf(t, type).profile = profile;
    }

    bool Save(const std::string& type)
    {
        loco::SpeedProfile p;
        {
            Table&           t = Types_();
            std::scoped_lock guard(t.lock);
            const Entry&     e = EntryOf(t, type);
            if (!e.profile) return true;
            p = *e.profile;
        }
        const std::string section = loco::SpeedSection(type);
        const std::array<std::pair<const char*, float>, 6> keys{ {
            { "fWalkForward", p.walk.forward }, { "fWalkSide", p.walk.side }, { "fWalkBack", p.walk.back },
            { "fRunForward", p.run.forward },   { "fRunSide", p.run.side },   { "fRunBack", p.run.back },
        } };
        bool ok = true;
        for (const auto& [key, value] : keys) ok = core::debug::SetValue(section, key, std::format("{:g}", value)) && ok;
        if (ok) LOG_INFO("speed profile '{}' saved to settings.ini [{}]", type, section);
        return ok;
    }

    bool Reset(const std::string& type)
    {
        const std::string section = loco::SpeedSection(type);
        bool              ok      = true;
        for (const char* key : { "fWalkForward", "fWalkSide", "fWalkBack", "fRunForward", "fRunSide", "fRunBack" })
            ok = core::debug::RemoveValue(section, key) && ok;
        Table&           t = Types_();
        std::scoped_lock guard(t.lock);
        EntryOf(t, type).profile = loco::DefaultSpeedProfile(type);
        return ok;
    }

}  // namespace CB::feature::controller::speed
