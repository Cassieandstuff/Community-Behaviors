#include "PCH.h"

#include "features/Foot IK/BodySolve.h"
#include "features/Foot IK/Rig.h"

#include "core/debug/Settings.h"

#include <platform/hooks/Override.h>

#include <Animation/Ik/TwoJoints/hkaTwoJointsIkSolver.h>
#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriverInfo.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Modifier/FootIk/hkbFootIkGains.h>

#include <PluginLogger.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// @file
/// @brief CB's foot IK driver: an Override of hkbFootIkDriver::doFootIk, which every character with foot IK data runs
///        after its graph (hkbFootIkSceneModifier, and BShkbAnimationGraph's world-from-model step), plus Overrides of the
///        out-of-line pieces vanilla's doFootIk calls: the world-from-model tilt toward the ground, the vertical feedback,
///        and (while a doFootIk runs on the thread) the two-joint bend of each leg.
/// @note  Owning the driver through its pieces, not by matching or porting doFootIk: doFootIk is 1420 decompiled lines of
///        inlined hkaPose bookkeeping, and its sibling hkbFootIkModifier::modify, written in full in the cbmatch
///        workbench, stays parked on a per-unit inline (STRUCTURAL 10.1%).
/// @note  Steps 4-6: vanilla computes every leg's ankle target and hoof roll, and sinks the whole body, as always; CB
///        defers the legs' bends, and once vanilla returns bends a spine for the difference between the fore and hind
///        ends and every leg with FABRIK over its full chain (BodySolve). A rig with a hind and a fore anchor tilts toward
///        the ground through CB's AlignToGround. Each piece has its own flag (foot_ik_legs, foot_ik_body, foot_ik_tilt)
///        for isolating it in game. The rig, one frame's targets and the first call of each piece are logged once.

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
            const hk::hkbCharacter*        character = nullptr;
            const hk::hkaSkeleton*         skeleton  = nullptr;
            const hk::hkbFootIkDriverInfo* info      = nullptr;
            std::shared_ptr<const Rig>     rig;
            /// @brief The legs' bends vanilla asked for and CB deferred, solved once vanilla returns.
            std::array<LegRequest, 8>      requests;
            int                            numRequests = 0;
        };

        /// @brief A rig derived once per foot IK data and skeleton.
        /// @note  Keyed by the data's address but checked against the skeleton and every leg's bones on each lookup: a
        ///        project unloaded and another loaded at the same address derives afresh instead of reusing stale bones.
        ///        Shared, so a call still holding the old rig keeps it alive across that swap.
        struct RigCache {
            struct Entry {
                const hk::hkaSkeleton*     skeleton = nullptr;
                std::vector<hk::hkInt16>   bones;   ///< every leg's hip, knee and ankle
                std::shared_ptr<const Rig> rig;
            };
            std::mutex                              lock;
            std::unordered_map<const void*, Entry>  entries;
        };

        /// @note Never destroyed: a graph may still run during static destruction at exit.
        RigCache& Rigs()
        {
            static RigCache* cache = new RigCache;
            return *cache;
        }

        std::vector<hk::hkInt16> LegBones(const hk::hkbFootIkDriverInfo& info)
        {
            std::vector<hk::hkInt16> bones;
            for (int i = 0; i < info.m_legs.getSize(); i++)
                bones.insert(bones.end(), { info.m_legs[i].m_hipIndex, info.m_legs[i].m_kneeIndex, info.m_legs[i].m_ankleIndex });
            return bones;
        }

        /// @return Null when the data cannot be used (Rig::problem set).
        std::shared_ptr<const Rig> RigFor(const hk::hkaSkeleton& skeleton, const hk::hkbFootIkDriverInfo& info)
        {
            RigCache&                      cache = Rigs();
            std::vector<hk::hkInt16>       bones = LegBones(info);
            std::scoped_lock               lock(cache.lock);
            RigCache::Entry&               entry = cache.entries[&info];
            if (!entry.rig || entry.skeleton != &skeleton || entry.bones != bones) {
                entry.skeleton = &skeleton;
                entry.bones    = std::move(bones);
                entry.rig      = std::make_shared<const Rig>(DeriveRig(skeleton, info));
            }
            return entry.rig->problem.empty() ? entry.rig : nullptr;
        }

        const RigLeg* LegEndingAt(const Rig& rig, hk::hkInt16 ankle)
        {
            for (const RigLeg& leg : rig.legs)
                if (!leg.chain.empty() && leg.chain.back() == ankle) return &leg;
            return nullptr;
        }

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

        void LogBendsOnce(const Call& call, float errorUpDownBias)
        {
            const hk::hkaSkeleton* skeleton = call.skeleton;
            if (!call.character || !skeleton || !call.numRequests || !FirstOf(&Logged::bends, call.info)) return;
            std::string bends;
            for (int i = 0; i < call.numRequests; i++) {
                const hk::hkInt16 ankle = call.requests[i].setup.m_endBoneIdx;
                const char*       name  = ankle >= 0 && ankle < skeleton->m_bones.getSize() ? skeleton->m_bones[ankle].m_name.cString() : nullptr;
                const __m128&     t     = call.requests[i].setup.m_endTargetMS.m_quad;
                bends += std::format(" {} ({:.1f} {:.1f} {:.1f})", name ? name : "?", t.m128_f32[0], t.m128_f32[1], t.m128_f32[2]);
            }
            LOG_INFO("foot IK: {} bends one frame (errorUpDownBias {:.2f}), ankle targets in model space:{}", CharacterName(*call.character),
                     errorUpDownBias, bends);
        }

        /// @brief The graph's errorUpDownBias (vanilla's sink blends its legs' errors by it); 0.5 without the controls
        ///        track.
        float ErrorUpDownBias(hk::hkbGeneratorOutput& output)
        {
            if (!output.isValid(hk::hkbGeneratorOutput::TRACK_FOOT_IK_CONTROLS)) return 0.5f;
            const auto* controls = static_cast<const hk::hkbFootIkControlData*>(output.getTrackData(hk::hkbGeneratorOutput::TRACK_FOOT_IK_CONTROLS));
            return controls->m_gains.m_errorUpDownBias;
        }

        /// @note Read once: the flags are settings.ini's, fixed for the session.
        bool LegsOn()
        {
            static const bool on = core::debug::FlagOn("foot_ik_legs");
            return on;
        }
        bool BodyOn()
        {
            static const bool on = core::debug::FlagOn("foot_ik_body");
            return on;
        }
        bool TiltOn()
        {
            static const bool on = core::debug::FlagOn("foot_ik_tilt");
            return on;
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
                call.character = context.m_character;
                call.info      = InfoOf(*context.m_character, call.skeleton);
                if (call.info) call.rig = RigFor(*call.skeleton, *call.info);
                LogRigOnce(*context.m_character);
            }
            {
                struct Scope {
                    explicit Scope(Call* c) { t_call = c; }
                    ~Scope() { t_call = nullptr; }
                } scope(&call);
                next(driver, context, generatorOutput);
            }
            if (!call.rig || !call.numRequests) return;
            BodyInputs inputs;
            inputs.errorUpDownBias = ErrorUpDownBias(generatorOutput);
            inputs.bendSpine       = BodyOn();
            if (const hk::hkbCharacterSetup* setup = context.m_character ? context.m_character->m_setup.val() : nullptr; setup && setup->m_data) {
                const __m128& up = setup->m_data->m_modelUpMS.m_quad;
                inputs.modelUpMS = { up.m128_f32[0], up.m128_f32[1], up.m128_f32[2] };
            }
            LogBendsOnce(call, inputs.errorUpDownBias);
            SolveBodyAndLegs(*call.rig, *call.skeleton, std::span(call.requests.data(), call.numRequests), inputs, generatorOutput);
        }

        /// @brief Defers the bend of every leg CB's rig knows: recorded with its target, left unsolved until vanilla's
        ///        driver returns and all the legs' targets are known (the body moves for all of them at once).
        /// @note  Vanilla's doFootPlacement ignores what the solve returns, reads the ankle straight after only to roll it
        ///        to the ground (a model-space rotation, kept through CB's solve), and asks for the end position, never the
        ///        end rotation.
        hk::hkBool OnBend(Bend::Next next, const hk::hkaTwoJointsIkSolver::Setup& setup, hk::hkaPose& pose)
        {
            Call* call = t_call;
            if (!call || !call->rig || !LegsOn()) return next(setup, pose);
            const RigLeg* leg = LegEndingAt(*call->rig, setup.m_endBoneIdx);
            if (!leg || !setup.m_enforceEndPosition || setup.m_enforceEndRotation || call->numRequests >= static_cast<int>(call->requests.size()))
                return next(setup, pose);
            call->requests[call->numRequests++] = { leg, setup };
            return true;
        }

        /// @note CB's tilt for a rig with a hind and a fore anchor (the horse); any other rig keeps vanilla's.
        void OnAlign(Align::Next next, hk::hkbFootIkDriver* driver, const hk::hkbContext& context, const hk::hkVector4& upWS,
                     hk::hkbGeneratorOutput& generatorOutput, const hk::hkbFootIkGains& gains)
        {
            static std::atomic<bool> logged{ false };
            LogFirstCall(logged, "world-from-model tilt");
            const Call*                  call  = t_call;
            const hk::hkbCharacterSetup* setup = context.m_character ? context.m_character->m_setup.val() : nullptr;
            if (TiltOn() && call && call->rig && setup && setup->m_data &&
                AlignToGround(*call->rig, *driver, setup->m_data->m_modelUpMS, setup->m_data->m_modelRightMS, generatorOutput, gains))
                return;
            next(driver, context, upWS, generatorOutput, gains);
        }

        /// @note Passes through: vanilla's sink owns the whole body's height (BodySolve only bends spines for the
        ///       difference between the fore and hind ends).
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
