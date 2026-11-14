#include "PCH.h"

#include "CameraRules.h"

#include <config/Config.h>
#include <core/debug/Settings.h>
#include <platform/startup/Startup.h>

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace CB::feature::camera::rules {

    namespace {

        namespace loco = hky::locomotion;
        using loco::OutputKind;

        constexpr float kHalflife = 0.4f;

        constexpr std::array<loco::OutputSpec, Index(Output::Count)> kOutputs{ {
            { "distance", OutputKind::Scale, kHalflife },
            { "maxDistance", OutputKind::Cap, kHalflife },
            { "zoomMinDistance", OutputKind::Set, kHalflife },
            { "zoomMaxDistance", OutputKind::Set, kHalflife },
            { "preferredDistance", OutputKind::Set, kHalflife },
            { "fov", OutputKind::Set, kHalflife },
            { "fovScale", OutputKind::Scale, kHalflife },
            { "shoulderScale", OutputKind::Scale, kHalflife },
            { "shoulderX", OutputKind::Add, kHalflife },
            { "shoulderZ", OutputKind::Add, kHalflife },
            { "anchorLag", OutputKind::Scale, kHalflife },
        } };

        constexpr const char* kRulesDir = "Data/SKSE/Plugins/Community Behaviors/Camera";

        bool On()
        {
            static const bool on = core::debug::FlagOn("camera_rules");
            return on;
        }

        /// @note Only the camera's update steps it (one thread), but Reload may come from a menu.
        struct Table {
            std::mutex                           lock;
            std::shared_ptr<const loco::RuleSet> rules;
            loco::ModifierSmoother               smoother{ kSchema };
            std::vector<std::string>             active;
            std::vector<std::string>             zoomRules;   ///< the rules stating a zoom output last frame
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
                else LOG_WARN("camera rules: {} could not be read", src.key);
            }
            auto set = std::make_shared<loco::RuleSet>(loco::LoadRules(docs, kSchema));
            for (const std::string& e : set->errors) LOG_WARN("camera rules: {}", e);
            int live = 0;
            for (const loco::Rule& r : set->rules) {
                if (!r.error.empty()) LOG_WARN("camera rules: '{}' ({}) never applies: {}", r.id, r.sources.back(), r.error);
                else if (r.Live()) ++live;
            }
            LOG_INFO("camera rules: {} rules from {} files ({} live)", set->rules.size(), docs.size(), live);
            return set;
        }

        void LoadAtStartup()
        {
            if (!On()) return;
            auto             set = Load();
            std::scoped_lock guard(T().lock);
            T().rules = std::move(set);
        }

        [[maybe_unused]] const bool s_loaded = platform::startup::OnDataLoaded(&LoadAtStartup, "camera rules");

    }  // namespace

    const loco::RuleSchema kSchema{ "camera", kOutputs };

    Result Step(RE::Actor& player, float dt)
    {
        Result out;
        if (!On()) return out;
        Table&           t = T();
        std::scoped_lock guard(t.lock);
        if (!t.rules) return out;

        hky::relay::EvalContext ctx;
        ctx.Reset(&player);
        const loco::ModifierGoals goals = loco::Evaluate(*t.rules, ctx);
        t.smoother.Step(goals, dt);

        /// @note Diagnostic: each time the rules holding change.
        if (!std::ranges::equal(goals.active, t.active)) {
            std::string ids;
            for (const std::string_view id : goals.active) ids += (ids.empty() ? "" : ", ") + std::string(id);
            LOG_INFO("camera rules: holding {}", ids.empty() ? "none" : ids);
            t.active.assign(goals.active.begin(), goals.active.end());
        }

        constexpr std::array kZoomOutputs{ Output::Distance, Output::MaxDistance, Output::ZoomMinDistance, Output::ZoomMaxDistance,
                                           Output::PreferredDistance };
        std::vector<std::string> zoomRules;
        for (const loco::Rule& r : t.rules->rules) {
            if (std::ranges::find(goals.active, std::string_view(r.id)) == goals.active.end()) continue;
            if (std::ranges::any_of(kZoomOutputs, [&](Output o) { return Index(o) < r.values.size() && r.values[Index(o)].has_value(); }))
                zoomRules.push_back(r.id);
        }
        ZoomGoals& z = out.zoom;
        z.scale      = goals.value[Index(Output::Distance)];
        z.min        = goals.value[Index(Output::ZoomMinDistance)];
        z.max        = goals.value[Index(Output::ZoomMaxDistance)];
        z.cap        = goals.value[Index(Output::MaxDistance)];
        z.preferred  = goals.value[Index(Output::PreferredDistance)];
        z.changed    = zoomRules != t.zoomRules;
        t.zoomRules  = std::move(zoomRules);
        out.state    = t.smoother.State();
        return out;
    }

    void Reload()
    {
        if (!On()) return;
        auto             set = Load();
        std::scoped_lock guard(T().lock);
        T().rules = std::move(set);
        T().smoother.Reset();
    }

}  // namespace CB::feature::camera::rules
