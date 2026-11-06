#include <locomotion/SpeedProfile.h>

#include <array>
#include <charconv>
#include <utility>

namespace CB::engine::locomotion {

    namespace {

        constexpr DirectionSpeeds kWalk{ 148.0f, 133.0f, 111.0f };
        constexpr DirectionSpeeds kRun{ 370.0f, 259.0f, 222.0f };
        constexpr DirectionSpeeds kSprint{ 518.0f, 0.0f, 0.0f };   ///< GASP has no sideways or backward sprint
        constexpr DirectionSpeeds kCrouch{ 166.5f, 148.0f, 133.0f };
        constexpr DirectionSpeeds kRelaxedCrouch{ 122.0f, 122.0f, 122.0f };

        /// @note The names are the MOVT records' MNAM, byte-exact (the engine looks them up from the iState_ variables).
        constexpr std::array<std::pair<std::string_view, SpeedProfile>, 8> kDefaults{ {
            { "NPCDefault", { kWalk, kRun } },
            { "NPC1HM", { kWalk, kRun } },
            { "NPC2HM", { kWalk, kRun } },
            { "NPCBow", { kWalk, kRun } },
            { "NPCMagic", { kWalk, kRun } },
            { "NPCSneaking", { kRelaxedCrouch, kCrouch } },
            { "NPCSprinting", { kSprint, kSprint } },
            { "AIControlledNPCSprinting", { kSprint, kSprint } },
        } };

        bool ReadSpeed(const std::function<std::string(std::string_view)>& lookup, std::string_view key, float& out)
        {
            const std::string text = lookup(key);
            if (text.empty()) return false;
            float      v   = 0.0f;
            const auto end = text.data() + text.size();
            const auto r   = std::from_chars(text.data(), end, v);
            if (r.ec != std::errc{} || v < 0.0f) return false;
            out = v;
            return true;
        }

    }  // namespace

    std::string SpeedSection(std::string_view type) { return "Speed." + std::string(type); }

    std::optional<SpeedProfile> DefaultSpeedProfile(std::string_view type)
    {
        for (const auto& [name, profile] : kDefaults)
            if (name == type) return profile;
        return std::nullopt;
    }

    std::optional<SpeedProfile> ResolveSpeedProfile(std::string_view type, const std::function<std::string(std::string_view)>& lookup)
    {
        std::optional<SpeedProfile> base = DefaultSpeedProfile(type);
        SpeedProfile                p    = base.value_or(SpeedProfile{});
        bool                        any  = base.has_value();
        const std::array<std::pair<std::string_view, float*>, 6> keys{ {
            { "fWalkForward", &p.walk.forward }, { "fWalkSide", &p.walk.side }, { "fWalkBack", &p.walk.back },
            { "fRunForward", &p.run.forward },   { "fRunSide", &p.run.side },   { "fRunBack", &p.run.back },
        } };
        for (const auto& [key, field] : keys) any = ReadSpeed(lookup, key, *field) || any;
        if (!any) return std::nullopt;
        return p;
    }

    float SpeedFactor(float wanted, float record) { return wanted > 0.0f && record > 0.0f ? wanted / record : 1.0f; }

}  // namespace CB::engine::locomotion
