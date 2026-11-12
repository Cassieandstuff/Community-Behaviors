#include "PCH.h"

#include "CharMovementTuning.h"

#include <core/debug/Settings.h>

#include <array>
#include <charconv>
#include <format>
#include <mutex>
#include <string>

namespace CB::feature::controller::tuning {

    namespace {

        using Profile = hky::locomotion::Profile;

        constexpr float kDeg = 57.2957795f;

        constexpr std::array kFields{
            Field{ "fAccel", "Acceleration", "units/s^2", &Profile::accel, 1.0f, 3000.0f },
            Field{ "fSprintAccelSlow", "Sprint acceleration (slow end)", "units/s^2", &Profile::sprintAccelSlow, 1.0f, 3000.0f },
            Field{ "fFriction", "Ground friction", "1/s", &Profile::friction, 1.0f, 20.0f },
            Field{ "fSprintFrictionLow", "Sprint friction (slow end)", "1/s", &Profile::sprintFrictionLow, 1.0f, 20.0f },
            Field{ "fBrakingNoInput", "Braking without input", "units/s^2", &Profile::brakingNoInput, 1.0f, 5000.0f },
            Field{ "fBrakingWithInput", "Braking with input (over the cap)", "units/s^2", &Profile::brakingWithInput, 1.0f, 5000.0f },
            Field{ "fBrakingFrictionFactor", "Braking friction factor", "x friction", &Profile::brakingFrictionFactor, 1.0f, 5.0f },
            Field{ "fFacingHalflife", "Facing halflife", "s", &Profile::facingHalflife, 1.0f, 1.0f },
            Field{ "fTurnInPlaceRate", "Turn in place rate", "deg/s", &Profile::turnInPlaceRate, kDeg, 720.0f },
            Field{ "fAirTurnRate", "Air turn rate", "deg/s", &Profile::airTurnRate, kDeg, 720.0f },
            Field{ "fLateralGrip", "Lateral grip (0 = off)", "units/s^2", &Profile::lateralGrip, 1.0f, 5000.0f },
            Field{ "fBrakingGrip", "Braking grip (0 = off)", "units/s^2", &Profile::brakingGrip, 1.0f, 5000.0f },
            Field{ "fMinTurnRadius", "Minimum turn radius", "units", &Profile::minTurnRadius, 1.0f, 1000.0f },
        };

        constexpr std::string_view kSection = "Movement";

        struct Store {
            std::mutex lock;
            Profile    profile;
            bool       loaded = false;
        };

        Store& S()
        {
            static Store s;
            return s;
        }

        /// @pre `s.lock` held.
        void LoadOnce(Store& s)
        {
            if (s.loaded) return;
            s.loaded = true;
            for (const Field& f : kFields) {
                const std::string text = core::debug::Value(kSection, f.key);
                float             v    = 0.0f;
                const auto [end, ec]   = std::from_chars(text.data(), text.data() + text.size(), v);
                if (text.empty() || ec != std::errc{} || v < 0.0f) continue;
                s.profile.*f.member = v / f.shown;
                LOG_INFO("movement tuning: {} = {:g} {} (settings.ini [Movement])", f.key, v, f.unit);
            }
        }

    }  // namespace

    std::span<const Field> Fields() { return kFields; }

    Profile Current()
    {
        Store&           s = S();
        std::scoped_lock guard(s.lock);
        LoadOnce(s);
        return s.profile;
    }

    void Set(const Profile& profile)
    {
        Store&           s = S();
        std::scoped_lock guard(s.lock);
        s.loaded  = true;
        s.profile = profile;
    }

    bool Save()
    {
        const Profile now      = Current();
        const Profile defaults = hky::locomotion::kPlayer;
        bool          ok       = true;
        for (const Field& f : kFields) {
            if (now.*f.member == defaults.*f.member) ok = core::debug::RemoveValue(kSection, f.key) && ok;
            else ok = core::debug::SetValue(kSection, f.key, std::format("{:g}", now.*f.member * f.shown)) && ok;
        }
        LOG_INFO("movement tuning: saved to settings.ini [Movement]{}", ok ? "" : " (with errors)");
        return ok;
    }

    void Reset() { Set(hky::locomotion::kPlayer); }

}  // namespace CB::feature::controller::tuning
