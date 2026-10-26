#include "PCH.h"

#include "features/Foot IK/Rig.h"

#include <platform/hooks/Override.h>
#include <platform/ik/Fabrik.h>

#include <Animation/Ik/TwoJoints/hkaTwoJointsIkSolver.h>
#include <Animation/Rig/hkaPose.h>
#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriverInfo.h>

#include <PluginLogger.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <memory>
#include <mutex>
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
/// @note  Step 4: each leg's bend is FABRIK over its full chain (platform/ik) toward vanilla's own ankle target, in
///        place of the two-joint solve; vanilla's targets, hoof roll, tilt and feedback are untouched. The rig, one
///        frame's targets and the first call of each piece are logged once.

namespace CB::feature::footik {

    namespace {

        namespace hk    = CB::havok;
        namespace hooks = platform::hooks;
        namespace ik    = platform::ik;

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
            std::shared_ptr<const Rig>     rig;
            std::array<LegBend, 8>         bends;
            int                            numBends  = 0;
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

        ik::Vec3 ToIk(const hk::hkVector4& v) { return { v.m_quad.m128_f32[0], v.m_quad.m128_f32[1], v.m_quad.m128_f32[2] }; }
        ik::Quat ToIk(const hk::hkQuaternion& q)
        {
            return { q.m_vec.m_quad.m128_f32[0], q.m_vec.m_quad.m128_f32[1], q.m_vec.m_quad.m128_f32[2], q.m_vec.m_quad.m128_f32[3] };
        }
        hk::hkQuaternion ToHk(const ik::Quat& q)
        {
            hk::hkQuaternion out;
            out.m_vec.m_quad = _mm_setr_ps(q.x, q.y, q.z, q.w);
            return out;
        }

        const RigLeg* LegEndingAt(const Rig& rig, hk::hkInt16 ankle)
        {
            for (const RigLeg& leg : rig.legs)
                if (!leg.chain.empty() && leg.chain.back() == ankle) return &leg;
            return nullptr;
        }

        /// @brief The hinge at vanilla's knee bone takes the data's knee range: the two-joint setup's cosine clamp on the
        ///        knee's interior angle, as a bend from straight.
        /// @note  Vanilla measures the angle between the hip-knee and knee-ankle chords; here it limits the bend between
        ///        the knee bone and the bone below it, which matches where no bone sits between knee and ankle. The
        ///        animated bend is always allowed, so a pose already outside the range does not snap.
        void ApplyKneeRange(ik::Chain& chain, const RigLeg& leg, const hk::hkaTwoJointsIkSolver::Setup& setup, const hk::hkaPose& pose)
        {
            const auto knee = std::find(leg.chain.begin(), leg.chain.end(), setup.m_secondJointIdx);
            if (knee == leg.chain.end() || knee == leg.chain.begin() || knee + 1 == leg.chain.end()) return;
            ik::Hinge& hinge = chain.hinges[static_cast<std::size_t>(knee - leg.chain.begin()) - 1];

            constexpr float kPi     = 3.14159265f;
            const float     minBend = kPi - std::acos(std::clamp(setup.m_cosineMaxHingeAngle, -1.0f, 1.0f));
            const float     maxBend = kPi - std::acos(std::clamp(setup.m_cosineMinHingeAngle, -1.0f, 1.0f));
            if (hinge.minBend < 0.0f || ik::Dot(hinge.axis, hinge.axis) < 0.25f) {
                const hk::hkQsTransform& kneeMS = pose.getBoneModelSpace(setup.m_secondJointIdx);
                hinge.axis    = ik::Normalized(ik::Rotate(ToIk(kneeMS.m_rotation), ToIk(setup.m_hingeAxisLS)), hinge.axis);
                hinge.minBend = -maxBend;
                hinge.maxBend = maxBend;
                return;
            }
            const std::size_t i        = static_cast<std::size_t>(knee - leg.chain.begin());
            const ik::Vec3    parent   = ik::Normalized(chain.source[i] - chain.source[i - 1]);
            const ik::Vec3    child    = ik::Normalized(chain.source[i + 1] - chain.source[i]);
            const float       animated = std::atan2(ik::Dot(hinge.axis, ik::Cross(parent, child)), ik::Dot(parent, child));
            hinge.minBend              = std::min(std::max(0.0f, minBend), animated);
            hinge.maxBend              = std::max(std::min(ik::kDefaultMaxBend, maxBend), animated);
        }

        /// @brief Bends `leg` toward vanilla's ankle target with FABRIK, writing the chain's model rotations into `pose`
        ///        (each with PROPAGATE, so the bones below follow).
        /// @note  The end bone's own rotation is left to doFootPlacement, which sets the ankle's after this returns
        ///        (vanilla's hoof roll to the ground).
        void SolveLeg(const RigLeg& leg, const hk::hkaTwoJointsIkSolver::Setup& setup, hk::hkaPose& pose)
        {
            const float weight = std::clamp(setup.m_secondJointIkGain, 0.0f, 1.0f);
            if (weight <= 0.0f || leg.chain.size() < 2) return;

            std::vector<ik::Vec3> joints;
            std::vector<ik::Quat> rotations;
            for (const hk::hkInt16 bone : leg.chain) {
                const hk::hkQsTransform& ms = pose.getBoneModelSpace(bone);
                joints.push_back(ToIk(ms.m_translation));
                rotations.push_back(ToIk(ms.m_rotation));
            }
            ik::Chain chain   = ik::MakeChain(joints);
            chain.holdEndBone = leg.chain.size() >= 4;
            ApplyKneeRange(chain, leg, setup, pose);

            const ik::Vec3 ankle  = joints.back();
            const ik::Vec3 target = ankle + (ToIk(setup.m_endTargetMS) - ankle) * weight;
            ik::Solve(chain, target);

            const std::vector<ik::Quat> solved = ik::AimRotations(chain, rotations);
            for (std::size_t i = 0; i + 1 < leg.chain.size(); i++)
                pose.accessBoneModelSpace(leg.chain[i], hk::hkaPose::PROPAGATE).m_rotation = ToHk(solved[i]);
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
                if (call.info) call.rig = RigFor(*skeleton, *call.info);
                LogRigOnce(*context.m_character);
            }
            struct Scope {
                explicit Scope(Call* c) { t_call = c; }
                ~Scope() { t_call = nullptr; }
            } scope(&call);
            next(driver, context, generatorOutput);
            LogBendsOnce(call);
        }

        /// @note Vanilla's doFootPlacement ignores what the solve returns (it reads the pose straight after), and asks only
        ///       for the end position (never the end rotation).
        hk::hkBool OnBend(Bend::Next next, const hk::hkaTwoJointsIkSolver::Setup& setup, hk::hkaPose& pose)
        {
            Call* call = t_call;
            if (!call) return next(setup, pose);
            if (call->numBends < static_cast<int>(call->bends.size())) call->bends[call->numBends++] = { setup.m_endBoneIdx, setup.m_endTargetMS };
            const RigLeg* leg = call->rig ? LegEndingAt(*call->rig, setup.m_endBoneIdx) : nullptr;
            if (!leg || !setup.m_enforceEndPosition || setup.m_enforceEndRotation) return next(setup, pose);
            SolveLeg(*leg, setup, pose);
            return true;
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
