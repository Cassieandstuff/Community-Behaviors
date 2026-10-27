#pragma once

#include "CharMovementModel.h"

#include <array>
#include <cstdint>
#include <string_view>

/// @file
/// @brief The controller's two tiers (spec H12): a character's PROFILE governs it from the top down (the player's, the
///        horse's: the movement model's numbers and the components it allows), and COMPONENTS push onto it to drive a
///        mode (swimming first).
/// @note  The contract: a component may (1) override or scale the profile's numbers, (2) add or reinterpret input axes,
///        (3) add motion-matching tag weights, (4) own the exclusive support channel (one owner at a time: the ground
///        layer unless a component takes it), and (5) is active only while a vanilla signal says so (swimming: the
///        controller in its Swimming state), so vanilla decides when and CB decides what. The profile shapes a component
///        (the horse swims at the surface, the player may dive).

namespace CB::feature::controller::component {

    /// @brief Which character's profile the frame starts from.
    enum class Character : std::uint8_t { Player, Horse };

    /// @brief The exclusive support channel's owner: what holds the character up and moves it vertically.
    enum class Support : std::uint8_t { Ground, Swim };

    /// @brief A motion-matching tag a component prefers, and how strongly (0-1).
    /// @note Names compared byte-exact, as autotag writes clip tags.
    struct TagWeight {
        std::string_view tag;
        float            weight = 0.0f;
    };

    /// @brief One frame of the controller as the components see it: the profile's numbers, the caps and input the model
    ///        steps with, and what the components claimed.
    struct Frame {
        Character      character = Character::Player;
        move::Profile  profile;
        move::Caps     caps;
        move::Input    input;
        Support        support      = Support::Ground;
        bool           verticalAxis = false;   ///< the character moves vertically under its own control (diving)
        std::array<TagWeight, 4> tags{};
        int            tagCount = 0;
        std::array<const char*, 4> active{};   ///< the components applied, by name
        int            activeCount = 0;

        void AddTag(std::string_view tag, float weight)
        {
            if (tagCount < static_cast<int>(tags.size())) tags[tagCount++] = TagWeight{ tag, weight };
        }
    };

    /// @brief A component: when it is on (its vanilla signal) and what it does to the frame.
    struct Component {
        const char* name     = "";
        int         priority = 0;   ///< lower applies first
        bool (*active)(RE::Actor& actor) = nullptr;
        void (*apply)(RE::Actor& actor, Frame& frame) = nullptr;
    };

    /// @brief Adds `c` to the registry; from a component's static initializer.
    bool Register(const Component& c);

    /// @brief Applies every active component to `frame`, in priority order.
    /// @warning A second component claiming the support channel is refused (logged): one owner at a time.
    void Apply(RE::Actor& actor, Frame& frame);

}  // namespace CB::feature::controller::component
