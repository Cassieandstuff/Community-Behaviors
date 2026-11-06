#include "PCH.h"

#include "CharSpeedProfile.h"

#include "core/debug/Settings.h"

#include <locomotion/SpeedProfile.h>

#include <mutex>
#include <optional>
#include <unordered_map>

namespace CB::feature::controller::speed {

    namespace {

        namespace loco = CB::engine::locomotion;
        using Dir      = RE::Movement::SPEED_DIRECTIONS;

        struct Entry {
            std::optional<loco::SpeedProfile> profile;
            const RE::BGSMovementType*        record = nullptr;   ///< the form, whose speeds are the record's as loaded
        };

        /// @note Keyed by the pooled name's data pointer: the engine's own lookup of a type by name goes through the same pool.
        struct Table {
            std::mutex                                   lock;
            std::unordered_map<const char*, Entry>       byName;
            std::unordered_map<const char*, const RE::BGSMovementType*> forms;
            bool                                         formsRead = false;
        };
        Table& Types()
        {
            static Table t;
            return t;
        }

        const RE::BGSMovementType* RecordOf(Table& t, const RE::BSFixedString& name)
        {
            if (!t.formsRead) {
                auto* data = RE::TESDataHandler::GetSingleton();
                if (!data) return nullptr;
                for (auto* form : data->GetFormArray<RE::BGSMovementType>())
                    if (form && form->movementTypeData.typeName.data()) t.forms.emplace(form->movementTypeData.typeName.data(), form);
                t.formsRead = true;
            }
            const auto it = t.forms.find(name.data());
            return it == t.forms.end() ? nullptr : it->second;
        }

        /// @brief `actor`'s current movement type's profile and record; false when it has no profile or no record.
        bool Lookup(RE::Actor* actor, loco::SpeedProfile& profile, const RE::BGSMovementType*& record)
        {
            auto* process = actor ? actor->GetActorRuntimeData().currentProcess : nullptr;
            auto* high    = process ? process->high : nullptr;
            if (!high) return false;
            const RE::BSFixedString& name = high->currentMovementType.typeName;
            if (!name.data() || !*name.data()) return false;

            Table&           t = Types();
            std::scoped_lock guard(t.lock);
            auto             it = t.byName.find(name.data());
            if (it == t.byName.end()) {
                const std::string type(name.c_str());
                const std::string section = loco::SpeedSection(type);
                Entry             e;
                e.profile = loco::ResolveSpeedProfile(type, [&](std::string_view key) { return core::debug::Value(section, key); });
                e.record  = RecordOf(t, name);
                if (e.profile) {
                    const auto& p = *e.profile;
                    LOG_INFO("speed profile '{}': walk {:.0f}/{:.0f}/{:.0f}, run {:.0f}/{:.0f}/{:.0f} (forward/side/back; settings.ini [{}] over "
                             "the built-ins){}",
                             type, p.walk.forward, p.walk.side, p.walk.back, p.run.forward, p.run.side, p.run.back, section,
                             e.record ? "" : " - no movement type record of that name, so the engine's speed stands");
                }
                it = t.byName.emplace(name.data(), std::move(e)).first;
            }
            if (!it->second.profile || !it->second.record) return false;
            profile = *it->second.profile;
            record  = it->second.record;
            return true;
        }

        const loco::DirectionSpeeds& Column(const loco::SpeedProfile& p, Gait gait) { return gait == Gait::Walk ? p.walk : p.run; }
        int ColumnIndex(Gait gait) { return gait == Gait::Walk ? RE::Movement::MaxSpeeds::kWalk : RE::Movement::MaxSpeeds::kRun; }

    }  // namespace

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

}  // namespace CB::feature::controller::speed
