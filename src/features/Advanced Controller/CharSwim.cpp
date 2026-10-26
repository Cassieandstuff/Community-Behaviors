#include "PCH.h"

#include "CharComponents.h"

#include <core/debug/Settings.h>

/// @file
/// @brief The swim component (char_swim_component, spec H12): pushed while the controller is in vanilla's Swimming
///        state, it takes the support channel from the ground layer, eases the movement model for water and prefers
///        swim clips. The profile shapes it: the player may dive (a vertical axis), the horse swims at the surface.
/// @note  Vanilla still decides when the character swims (its Swimming state, entered by water depth) and still moves it
///        vertically: CB's ground states never wrap Swimming, so the channel's owner here only says who holds it. The
///        tag weight waits on motion matching reading it (build step 9).
/// @note  Its numbers are starting values, to tune against acceptance scenario S13.

namespace CB::feature::controller {

    namespace {

        /// @brief How water changes the profile: slower to speed up and to stop, no grip or dig-in (nothing to dig into),
        ///        a slower turn of the body.
        constexpr float kAccelScale    = 0.5f;
        constexpr float kFrictionScale = 0.6f;
        constexpr float kBrakingScale  = 0.5f;
        constexpr float kFacingScale   = 1.5f;

        bool Swimming(RE::Actor& actor)
        {
            static const bool on = core::debug::FlagOn("char_swim_component");
            if (!on) return false;
            const auto* ctrl = actor.GetCharController();
            return ctrl && ctrl->context.currentState == RE::hkpCharacterStateType::kSwimming;
        }

        void Apply(RE::Actor&, component::Frame& f)
        {
            f.support = component::Support::Swim;
            move::Profile& p    = f.profile;
            p.accel            *= kAccelScale;
            p.sprintAccelSlow  *= kAccelScale;
            p.friction         *= kFrictionScale;
            p.sprintFrictionLow *= kFrictionScale;
            p.brakingNoInput   *= kBrakingScale;
            p.brakingWithInput *= kBrakingScale;
            p.facingHalflife   *= kFacingScale;
            p.lateralGrip   = 0.0f;
            p.brakingGrip   = 0.0f;
            p.digInBraking  = 0.0f;
            f.verticalAxis  = f.character == component::Character::Player;
            f.AddTag("Swim", 1.0f);
        }

        [[maybe_unused]] const bool s_registered = component::Register({ "Swim", 0, &Swimming, &Apply });

    }  // namespace

}  // namespace CB::feature::controller
