#include "PCH.h"

#include "CharMovementTuning.h"

#include <core/debug/Settings.h>

#include <array>
#include <atomic>
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
            Field{ "fLateralGrip", "Lateral grip (0 = off, or traction's)", "units/s^2", &Profile::lateralGrip, 1.0f, 5000.0f },
            Field{ "fBrakingGrip", "Braking grip (0 = off, or traction's)", "units/s^2", &Profile::brakingGrip, 1.0f, 5000.0f },
            Field{ "fMinTurnRadius", "Minimum turn radius", "units", &Profile::minTurnRadius, 1.0f, 1000.0f },
            Field{ "fDigInAngle", "Dig-in from (0 = off, or traction's)", "deg", &Profile::digInAngle, kDeg, 180.0f },
            Field{ "fDigInBraking", "Dig-in braking at 180 deg", "units/s^2", &Profile::digInBraking, 1.0f, 5000.0f },
            Field{ "fDigInFacingHalflife", "Dig-in facing halflife", "s", &Profile::digInFacingHalflife, 1.0f, 1.0f },
            Field{ "fDigInMaxLead", "Dig-in facing lead (0 = no cap)", "deg", &Profile::digInMaxLead, kDeg, 180.0f },
        };

        using BodyProfile = hky::locomotion::BodyFacingProfile;

        /// @note GASP's CMC Steering numbers (locomotion::BodyFacingProfile).
        constexpr std::array kBodyFields{
            BodyField{ "fSteerProceduralTime", "Steering time with no turn in the clip (95 %)", "s", &BodyProfile::proceduralTime, 1.0f, 2.0f },
            BodyField{ "fSteerAnimatedTime", "Clip turn read ahead", "s", &BodyProfile::animatedTime, 1.0f, 10.0f },
            BodyField{ "fSteerTargetAhead", "Target: the model's facing ahead", "s", &BodyProfile::targetAhead, 1.0f, 1.0f },
            BodyField{ "fSteerMinScale", "Clip turn scale, least", "x", &BodyProfile::minScale, 1.0f, 1.0f },
            BodyField{ "fSteerMaxScale", "Clip turn scale, most", "x", &BodyProfile::maxScale, 1.0f, 3.0f },
            BodyField{ "fSteerMinSpeed", "Steering from speed", "units/s", &BodyProfile::minSpeed, 1.0f, 100.0f },
            BodyField{ "fBodyMaxOffset", "Body's largest offset from the model (ini: negative = none)", "deg", &BodyProfile::maxOffset, kDeg, 180.0f },
        };
        /// @brief Keys of the offset's old ease back toward the model (before steering): ignored, and removed on SaveBody.
        constexpr std::array<std::string_view, 3> kRetiredBodyKeys{ "fBodyHalflife", "fBodyHalflifeIdle", "fBodyClipOwnsFrom" };

        constexpr std::string_view kBodySection = "Character";

        struct BodyStore {
            std::mutex  lock;
            BodyProfile profile;
            bool        loaded = false;
        };

        BodyStore& B()
        {
            static BodyStore s;
            return s;
        }

        /// @pre `s.lock` held.
        /// @note A negative value is kept only for fBodyMaxOffset, where it means no clamp.
        void LoadBodyOnce(BodyStore& s)
        {
            if (s.loaded) return;
            s.loaded = true;
            for (const BodyField& f : kBodyFields) {
                const std::string text = core::debug::Value(kBodySection, f.key);
                float             v    = 0.0f;
                const auto [end, ec]   = std::from_chars(text.data(), text.data() + text.size(), v);
                if (text.empty() || ec != std::errc{} || (v < 0.0f && f.member != &BodyProfile::maxOffset)) continue;
                s.profile.*f.member = v / f.shown;
                LOG_INFO("body facing tuning: {} = {:g} {} (settings.ini [Character])", f.key, v, f.unit);
            }
        }

        /// @brief A live switch read from its flag on first use.
        struct Switch {
            std::atomic<int> state{ -1 };   ///< -1 = not read yet
            bool Get(std::string_view flag)
            {
                int now = state.load(std::memory_order_relaxed);
                if (now < 0) {
                    now = core::debug::FlagOn(flag) ? 1 : 0;
                    state.store(now, std::memory_order_relaxed);
                }
                return now != 0;
            }
            void Set(std::string_view key, bool on)
            {
                state.store(on ? 1 : 0, std::memory_order_relaxed);
                core::debug::SetValue(kBodySection, key, on ? "true" : "false");
            }
        };
        Switch g_weightedFacing, g_playerTraction;

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

    std::span<const BodyField> BodyFields() { return kBodyFields; }

    BodyProfile Body()
    {
        BodyStore&       s = B();
        std::scoped_lock guard(s.lock);
        LoadBodyOnce(s);
        return s.profile;
    }

    void SetBody(const BodyProfile& profile)
    {
        BodyStore&       s = B();
        std::scoped_lock guard(s.lock);
        s.loaded  = true;
        s.profile = profile;
    }

    bool SaveBody()
    {
        const BodyProfile now      = Body();
        const BodyProfile defaults = hky::locomotion::kPlayerBody;
        bool              ok       = true;
        for (const BodyField& f : kBodyFields) {
            if (now.*f.member == defaults.*f.member) ok = core::debug::RemoveValue(kBodySection, f.key) && ok;
            else ok = core::debug::SetValue(kBodySection, f.key, std::format("{:g}", now.*f.member * f.shown)) && ok;
        }
        for (const std::string_view key : kRetiredBodyKeys) ok = core::debug::RemoveValue(kBodySection, key) && ok;
        LOG_INFO("body facing tuning: saved to settings.ini [Character]{}", ok ? "" : " (with errors)");
        return ok;
    }

    void ResetBody() { SetBody(hky::locomotion::kPlayerBody); }

    bool WeightedFacing() { return g_weightedFacing.Get("char_weighted_facing"); }
    bool PlayerTraction() { return g_playerTraction.Get("char_player_traction"); }
    void SetWeightedFacing(bool on) { g_weightedFacing.Set("bWeightedFacing", on); }
    void SetPlayerTraction(bool on) { g_playerTraction.Set("bPlayerTraction", on); }

}  // namespace CB::feature::controller::tuning
