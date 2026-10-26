#include "PCH.h"

#include "features/Foot IK/Rig.h"

#include <platform/hooks/Override.h>

#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriverInfo.h>

#include <PluginLogger.h>

#include <mutex>
#include <unordered_set>

/// @file
/// @brief CB's foot IK driver: an Override of hkbFootIkDriver::doFootIk, which every character with foot IK data runs
///        after its graph (hkbFootIkSceneModifier, and BShkbAnimationGraph's world-from-model step).
/// @note  Step 1: derives each character's rig and logs it once per foot IK data, then runs vanilla's driver.

namespace CB::feature::footik {

    namespace {

        namespace hk    = CB::havok;
        namespace hooks = platform::hooks;

        using DoFootIk = hooks::Override<&hk::hkbFootIkDriver::doFootIk>;

        /// @brief The foot IK data already logged.
        /// @note  Keyed by the data, not the character: every character of a project shares it, so a crowd of the same
        ///        creature logs once.
        struct Logged {
            std::mutex                      lock;
            std::unordered_set<const void*> seen;
        };

        /// @note Never destroyed: a graph may still run during static destruction at exit.
        Logged& Seen()
        {
            static Logged* logged = new Logged;
            return *logged;
        }

        void LogRigOnce(const hk::hkbCharacter& character)
        {
            const hk::hkbCharacterSetup* setup = character.m_setup;
            if (!setup || !setup->m_data || !setup->m_animationSkeleton) return;
            const hk::hkbFootIkDriverInfo* info = setup->m_data->m_footIkDriverInfo;
            if (!info) return;
            {
                Logged&          logged = Seen();
                std::scoped_lock lock(logged.lock);
                if (!logged.seen.insert(info).second) return;
            }
            const hk::hkaSkeleton& skeleton = *setup->m_animationSkeleton;
            const char*            name     = character.m_name.cString();
            LOG_INFO("foot IK: {} ({} legs): {}", name ? name : "?", info->m_legs.getSize(),
                     Describe(DeriveRig(skeleton, *info), skeleton));
        }

        void OnDoFootIk(DoFootIk::Next next, hk::hkbFootIkDriver* driver, const hk::hkbContext& context,
                        hk::hkbGeneratorOutput& generatorOutput)
        {
            if (context.m_character) LogRigOnce(*context.m_character);
            next(driver, context, generatorOutput);
        }

        [[maybe_unused]] const bool s_subscribed =
            DoFootIk::Subscribe(&OnDoFootIk, hooks::Priority::Normal, "FootIkDriver", "foot_ik");

    }  // namespace

}  // namespace CB::feature::footik
