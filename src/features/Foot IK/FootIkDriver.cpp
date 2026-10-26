#include "PCH.h"

#include "features/Foot IK/Rig.h"

#include <platform/hooks/Override.h>

#include <Animation/Ik/TwoJoints/hkaTwoJointsIkSolver.h>
#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriverInfo.h>

#include <PluginLogger.h>

#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <unordered_set>

/// @file
/// @brief CB's foot IK driver: an Override of hkbFootIkDriver::doFootIk, which every character with foot IK data runs
///        after its graph (hkbFootIkSceneModifier, and BShkbAnimationGraph's world-from-model step), plus Overrides of the
///        out-of-line pieces vanilla's doFootIk calls: the world-from-model tilt toward the ground, the vertical feedback,
///        and (while a doFootIk runs on the thread) the two-joint bend of each leg.
/// @note  Owning the driver through its pieces, not by matching or porting doFootIk: doFootIk is 1420 decompiled lines of
///        inlined hkaPose bookkeeping, and its sibling hkbFootIkModifier::modify, written in full in the cbmatch
///        workbench, stays parked on a per-unit inline (STRUCTURAL 10.1%).
/// @note  Step 3: every piece passes through to vanilla; each leg's bend target is recorded, and the rig, the targets
///        and the first call of each piece are logged once.

namespace CB::feature::footik {

    namespace {

        namespace hk    = CB::havok;
        namespace hooks = platform::hooks;

        using DoFootIk      = hooks::Override<&hk::hkbFootIkDriver::doFootIk>;
        using Bend          = hooks::Override<&hk::hkaTwoJointsIkSolver::solve>;
        using Align         = hooks::Override<&hk::hkbFootIkDriver::alignWorldFromModelUpWithGroundNormal>;
        using VerticalError = hooks::Override<&hk::hkbFootIkDriver::computeVerticalError>;

        /// @brief One doFootIk call on this thread, while vanilla's driver runs inside it.
        struct Call {
            /// @brief A leg's two-joint bend as vanilla asked for it.
            struct LegBend {
                hk::hkInt16   ankle;
                hk::hkVector4 targetMS;
            };

            const hk::hkbCharacter*        character = nullptr;
            const hk::hkbFootIkDriverInfo* info      = nullptr;
            std::array<LegBend, 8>         bends;
            int                            numBends  = 0;
        };

        /// @note Set only for the length of the doFootIk subscriber's call into vanilla: the two-joint solver is shared
        ///       with hand IK and hkbFootIkModifier, which must pass untouched.
        thread_local Call* t_call = nullptr;

        /// @brief The foot IK data already logged.
        /// @note  Keyed by the data, not the character: every character of a project shares it, so a crowd of the same
        ///        creature logs once.
        struct Logged {
            std::mutex                      lock;
            std::unordered_set<const void*> rigs, bends;
        };

        /// @note Never destroyed: a graph may still run during static destruction at exit.
        Logged& Seen()
        {
            static Logged* logged = new Logged;
            return *logged;
        }

        bool FirstOf(std::unordered_set<const void*> Logged::* set, const void* key)
        {
            Logged&          logged = Seen();
            std::scoped_lock lock(logged.lock);
            return (logged.*set).insert(key).second;
        }

        const char* CharacterName(const hk::hkbCharacter& character)
        {
            const char* name = character.m_name.cString();
            return name ? name : "?";
        }

        const hk::hkbFootIkDriverInfo* InfoOf(const hk::hkbCharacter& character, const hk::hkaSkeleton*& skeletonOut)
        {
            const hk::hkbCharacterSetup* setup = character.m_setup;
            if (!setup || !setup->m_data || !setup->m_animationSkeleton) return nullptr;
            skeletonOut = setup->m_animationSkeleton;
            return setup->m_data->m_footIkDriverInfo;
        }

        void LogRigOnce(const hk::hkbCharacter& character)
        {
            const hk::hkaSkeleton*         skeleton = nullptr;
            const hk::hkbFootIkDriverInfo* info     = InfoOf(character, skeleton);
            if (!info || !FirstOf(&Logged::rigs, info)) return;
            LOG_INFO("foot IK: {} ({} legs): {}", CharacterName(character), info->m_legs.getSize(),
                     Describe(DeriveRig(*skeleton, *info), *skeleton));
        }

        void LogBendsOnce(const Call& call)
        {
            const hk::hkaSkeleton* skeleton = nullptr;
            if (!call.character || !InfoOf(*call.character, skeleton) || !call.numBends || !FirstOf(&Logged::bends, call.info)) return;
            std::string bends;
            for (int i = 0; i < call.numBends; i++) {
                const auto&   b    = call.bends[i];
                const char*   name = b.ankle >= 0 && b.ankle < skeleton->m_bones.getSize() ? skeleton->m_bones[b.ankle].m_name.cString() : nullptr;
                const __m128& t    = b.targetMS.m_quad;
                bends += std::format(" {} ({:.1f} {:.1f} {:.1f})", name ? name : "?", t.m128_f32[0], t.m128_f32[1], t.m128_f32[2]);
            }
            LOG_INFO("foot IK: {} bends one frame, ankle targets in model space:{}", CharacterName(*call.character), bends);
        }

        void LogFirstCall(std::atomic<bool>& done, const char* what)
        {
            if (!done.exchange(true, std::memory_order_relaxed)) LOG_INFO("foot IK: vanilla's {} reached CB's Override", what);
        }

        void OnDoFootIk(DoFootIk::Next next, hk::hkbFootIkDriver* driver, const hk::hkbContext& context,
                        hk::hkbGeneratorOutput& generatorOutput)
        {
            Call call;
            if (context.m_character) {
                const hk::hkaSkeleton* skeleton = nullptr;
                call.character                  = context.m_character;
                call.info                       = InfoOf(*context.m_character, skeleton);
                LogRigOnce(*context.m_character);
            }
            struct Scope {
                explicit Scope(Call* c) { t_call = c; }
                ~Scope() { t_call = nullptr; }
            } scope(&call);
            next(driver, context, generatorOutput);
            LogBendsOnce(call);
        }

        hk::hkBool OnBend(Bend::Next next, const hk::hkaTwoJointsIkSolver::Setup& setup, hk::hkaPose& pose)
        {
            if (Call* call = t_call; call && call->numBends < static_cast<int>(call->bends.size()))
                call->bends[call->numBends++] = { setup.m_endBoneIdx, setup.m_endTargetMS };
            return next(setup, pose);
        }

        void OnAlign(Align::Next next, hk::hkbFootIkDriver* driver, const hk::hkbContext& context, const hk::hkVector4& upWS,
                     hk::hkbGeneratorOutput& generatorOutput, const hk::hkbFootIkGains& gains)
        {
            static std::atomic<bool> logged{ false };
            LogFirstCall(logged, "world-from-model tilt");
            next(driver, context, upWS, generatorOutput, gains);
        }

        hk::hkReal OnVerticalError(VerticalError::Next next, const hk::hkbFootIkDriver* driver, hk::hkReal errorUpDownBias)
        {
            static std::atomic<bool> logged{ false };
            LogFirstCall(logged, "vertical feedback");
            return next(driver, errorUpDownBias);
        }

        [[maybe_unused]] const bool s_doFootIk      = DoFootIk::Subscribe(&OnDoFootIk, hooks::Priority::Normal, "FootIkDriver", "foot_ik");
        [[maybe_unused]] const bool s_bend          = Bend::Subscribe(&OnBend, hooks::Priority::Normal, "FootIkDriver", "foot_ik");
        [[maybe_unused]] const bool s_align         = Align::Subscribe(&OnAlign, hooks::Priority::Normal, "FootIkDriver", "foot_ik");
        [[maybe_unused]] const bool s_verticalError = VerticalError::Subscribe(&OnVerticalError, hooks::Priority::Normal, "FootIkDriver", "foot_ik");

    }  // namespace

}  // namespace CB::feature::footik
