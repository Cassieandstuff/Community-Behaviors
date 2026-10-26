#include "PCH.h"

#include "features/Foot IK/BodySolve.h"

#include <platform/ik/Fabrik.h>

#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Modifier/FootIk/hkbFootIkGains.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace CB::feature::footik {

    namespace {

        namespace hk = CB::havok;
        namespace ik = platform::ik;

        constexpr float     kPi = 3.14159265f;
        constexpr ik::Params kLimb{ 20, 0.1f };
        constexpr ik::Params kSpine{ 20, 0.1f };

        ik::Vec3 V(const hk::hkVector4& v) { return { v.m_quad.m128_f32[0], v.m_quad.m128_f32[1], v.m_quad.m128_f32[2] }; }
        ik::Quat Q(const hk::hkQuaternion& q)
        {
            return { q.m_vec.m_quad.m128_f32[0], q.m_vec.m_quad.m128_f32[1], q.m_vec.m_quad.m128_f32[2], q.m_vec.m_quad.m128_f32[3] };
        }
        hk::hkQuaternion HkQ(const ik::Quat& q)
        {
            hk::hkQuaternion out;
            out.m_vec.m_quad = _mm_setr_ps(q.x, q.y, q.z, q.w);
            return out;
        }
        ik::Vec3 Mul(ik::Vec3 a, ik::Vec3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }

        /// @brief The most a spine joint folds beyond its animated bend: a horse's back is close to rigid, and a spine
        ///        free to fold (its joints are straight in the animation, so balls) zig-zagged and flung the head around.
        constexpr float kSpineGive = 10.0f * kPi / 180.0f;
        /// @brief The most a leg joint the animation holds straight bends either way (the knee excepted: the data says
        ///        its side).
        constexpr float kStraightLegJointGive = 30.0f * kPi / 180.0f;

        float Fold(const ik::Chain& chain, std::size_t joint)
        {
            const ik::Vec3 parent = ik::Normalized(chain.source[joint] - chain.source[joint - 1]);
            const ik::Vec3 child  = ik::Normalized(chain.source[joint + 1] - chain.source[joint]);
            return std::acos(std::clamp(ik::Dot(parent, child), -1.0f, 1.0f));
        }

        ik::Chain SpineChain(std::vector<ik::Vec3> joints)
        {
            ik::Chain chain = ik::MakeChain(joints);
            for (std::size_t i = 0; i < chain.hinges.size(); i++) chain.hinges[i] = { {}, 0.0f, Fold(chain, i + 1) + kSpineGive };
            return chain;
        }

        struct Xf {
            ik::Vec3 t;
            ik::Quat r;
            ik::Vec3 s{ 1.0f, 1.0f, 1.0f };
        };

        /// @note As Havok's hkQsTransform::setMul: the parent's scale does not reach the child's translation.
        Xf Compose(const Xf& parent, const Xf& local)
        {
            return { parent.t + ik::Rotate(parent.r, local.t), ik::Normalized(parent.r * local.r), Mul(parent.s, local.s) };
        }

        /// @brief The output's local pose taken into the model space vanilla's driver solved in, with edits written
        ///        straight back to the output.
        /// @note  Vanilla's doFootIk multiplies every bone's translation and the root bone's scale by the world-from-model's
        ///        scale (0x140bd3c40) and divides them back at the end; the targets it hands the leg bends are in that
        ///        scaled space.
        class Pose {
        public:
            Pose(const hk::hkaSkeleton& skeleton, hk::hkbGeneratorOutput& output) :
                m_parents(skeleton.m_parentIndices),
                m_out(output.accessPoseLocal()),
                m_scale(output.accessWorldFromModel().m_scale.m_quad.m128_f32[0])
            {
                const int numBones = std::min(output.getNumPoseLocal(), skeleton.m_parentIndices.getSize());
                m_local.resize(numBones);
                m_model.resize(numBones);
                for (int i = 0; i < numBones; i++) {
                    m_local[i] = { V(m_out[i].m_translation) * m_scale, Q(m_out[i].m_rotation), V(m_out[i].m_scale) };
                    if (i == 0) m_local[i].s = m_local[i].s * m_scale;
                }
                Update();
            }

            bool Valid(hk::hkInt16 bone) const { return bone >= 0 && bone < static_cast<int>(m_model.size()); }
            bool Usable() const { return m_scale > 1e-6f && !m_model.empty(); }
            const Xf& Model(hk::hkInt16 bone) const { return m_model[bone]; }

            /// @brief Every bone's model transform from the locals (parents come before their children).
            void Update()
            {
                for (std::size_t i = 0; i < m_local.size(); i++) {
                    const hk::hkInt16 parent = m_parents[static_cast<int>(i)];
                    m_model[i]               = parent >= 0 && parent < static_cast<int>(i) ? Compose(m_model[parent], m_local[i]) : m_local[i];
                }
            }

            /// @brief Turns `bone` to the model rotation `rotation`; its descendants keep their locals.
            /// @pre   Its parent's model rotation is current (set earlier in the same pass, or by Update).
            void SetModelRotation(hk::hkInt16 bone, const ik::Quat& rotation)
            {
                const hk::hkInt16 parent  = m_parents[bone];
                const ik::Quat    local   = parent >= 0 ? ik::Normalized(ik::Conjugate(m_model[parent].r) * rotation) : rotation;
                m_local[bone].r           = local;
                m_model[bone].r           = rotation;
                m_out[bone].m_rotation    = HkQ(local);
            }

            /// @brief Moves `bone` by `delta` in model space; its descendants follow. Positions are stale until Update.
            void MoveModel(hk::hkInt16 bone, const ik::Vec3& delta)
            {
                const hk::hkInt16 parent = m_parents[bone];
                const ik::Vec3    local  = parent >= 0 ? ik::Rotate(ik::Conjugate(m_model[parent].r), delta) : delta;
                m_local[bone].t += local;
                const ik::Vec3 out = local * (1.0f / m_scale);
                m_out[bone].m_translation.m_quad =
                    _mm_add_ps(m_out[bone].m_translation.m_quad, _mm_setr_ps(out.x, out.y, out.z, 0.0f));
            }

        private:
            const hk::hkArray<hk::hkInt16>& m_parents;
            hk::hkQsTransform*              m_out;
            float                           m_scale;
            std::vector<Xf>                 m_local, m_model;
        };

        std::vector<ik::Vec3> Positions(const Pose& pose, const std::vector<hk::hkInt16>& bones)
        {
            std::vector<ik::Vec3> out;
            for (const hk::hkInt16 bone : bones) out.push_back(pose.Model(bone).t);
            return out;
        }

        std::vector<ik::Quat> Rotations(const Pose& pose, const std::vector<hk::hkInt16>& bones)
        {
            std::vector<ik::Quat> out;
            for (const hk::hkInt16 bone : bones) out.push_back(pose.Model(bone).r);
            return out;
        }

        /// @brief The hinge at vanilla's knee bone takes the data's knee range: the two-joint setup's cosine clamp on the
        ///        knee's interior angle, as a bend from straight.
        /// @note  Vanilla measures the angle between the hip-knee and knee-ankle chords; here it limits the bend between
        ///        the knee bone and the bone below it, which matches where no bone sits between knee and ankle. The
        ///        animated bend is always allowed, so a pose already outside the range does not snap.
        void ApplyKneeRange(ik::Chain& chain, const RigLeg& leg, const hk::hkaTwoJointsIkSolver::Setup& setup, const ik::Quat& kneeRotation)
        {
            const auto knee = std::find(leg.chain.begin(), leg.chain.end(), setup.m_secondJointIdx);
            if (knee == leg.chain.end() || knee == leg.chain.begin() || knee + 1 == leg.chain.end()) return;
            const std::size_t i     = static_cast<std::size_t>(knee - leg.chain.begin());
            ik::Hinge&        hinge = chain.hinges[i - 1];

            const float minBend = std::max(0.0f, kPi - std::acos(std::clamp(setup.m_cosineMaxHingeAngle, -1.0f, 1.0f)));
            const float maxBend = std::min(ik::kDefaultMaxBend, kPi - std::acos(std::clamp(setup.m_cosineMinHingeAngle, -1.0f, 1.0f)));
            if (hinge.minBend < 0.0f || ik::Dot(hinge.axis, hinge.axis) < 0.25f) {
                const ik::Vec3 axis = ik::Normalized(ik::Rotate(kneeRotation, V(setup.m_hingeAxisLS)));
                if (ik::Dot(axis, axis) < 0.25f) return;
                hinge = { -axis, minBend, std::max(minBend, maxBend) };
                return;
            }
            const ik::Vec3 parent   = ik::Normalized(chain.source[i] - chain.source[i - 1]);
            const ik::Vec3 child    = ik::Normalized(chain.source[i + 1] - chain.source[i]);
            const float    animated = std::atan2(ik::Dot(hinge.axis, ik::Cross(parent, child)), ik::Dot(parent, child));
            hinge.minBend           = std::min(minBend, animated);
            hinge.maxBend           = std::max(maxBend, animated);
        }

        ik::Chain LegChain(const Pose& pose, const LegRequest& request)
        {
            const RigLeg& leg = *request.rigLeg;
            ik::Chain     chain = ik::MakeChain(Positions(pose, leg.chain));
            chain.holdEndBone   = leg.chain.size() >= 4;
            for (ik::Hinge& hinge : chain.hinges) {
                if (ik::Dot(hinge.axis, hinge.axis) < 0.25f) hinge.maxBend = kStraightLegJointGive;
                else if (hinge.minBend < 0.0f) hinge = { hinge.axis, -kStraightLegJointGive, kStraightLegJointGive };
            }
            ApplyKneeRange(chain, leg, request.setup, pose.Model(request.setup.m_secondJointIdx).r);
            return chain;
        }

        ik::Vec3 Target(const LegRequest& request, const ik::Vec3& ankle)
        {
            const float weight = std::clamp(request.setup.m_secondJointIkGain, 0.0f, 1.0f);
            return ankle + (V(request.setup.m_endTargetMS) - ankle) * weight;
        }

        bool Usable(const Pose& pose, const LegRequest& request)
        {
            if (!request.rigLeg || request.rigLeg->chain.size() < 2 || !pose.Valid(request.setup.m_secondJointIdx)) return false;
            for (const hk::hkInt16 bone : request.rigLeg->chain)
                if (!pose.Valid(bone)) return false;
            return true;
        }

    }  // namespace

    void SolveBodyAndLegs(const Rig& rig, const hk::hkaSkeleton& skeleton, std::span<const LegRequest> requests, float stiffness,
                          hk::hkbGeneratorOutput& output)
    {
        Pose pose(skeleton, output);
        if (!pose.Usable() || !pose.Valid(rig.bodyRoot)) return;

        std::vector<const LegRequest*> legs;
        for (const LegRequest& request : requests)
            if (Usable(pose, request)) legs.push_back(&request);
        if (legs.empty()) return;

        std::vector<ik::Quat> ankles;
        for (const LegRequest* leg : legs) ankles.push_back(pose.Model(leg->rigLeg->chain.back()).r);

        std::vector<std::vector<hk::hkInt16>> spineBones;
        for (const std::vector<hk::hkInt16>& spine : rig.spines) {
            spineBones.push_back(spine.empty() ? std::vector<hk::hkInt16>{ rig.bodyRoot } : spine);
            for (const hk::hkInt16 bone : spineBones.back())
                if (!pose.Valid(bone)) return;
        }

        if (stiffness > 0.0f && !spineBones.empty()) {
            ik::Body body;
            body.root = pose.Model(rig.bodyRoot).t;
            for (const std::vector<hk::hkInt16>& bones : spineBones) body.spines.push_back(SpineChain(Positions(pose, bones)));
            for (const LegRequest* leg : legs) {
                std::size_t anchor = spineBones.size();
                for (std::size_t a = 0; a < spineBones.size(); a++)
                    if (spineBones[a].back() == leg->rigLeg->anchor) anchor = a;
                ik::Chain chain  = LegChain(pose, *leg);
                const ik::Vec3 t = Target(*leg, chain.joints.back());
                body.limbs.push_back({ std::move(chain), anchor, t });
            }
            const ik::Vec3 root0 = body.root;
            ik::SolveBody(body, { std::clamp(stiffness, 0.0f, 1.0f), kLimb, kSpine });

            for (std::size_t a = 0; a < spineBones.size(); a++) {
                const std::vector<hk::hkInt16>& bones = spineBones[a];
                const ik::Chain&                chain = body.spines[a];
                if (bones.size() < 2) continue;
                std::vector<ik::Quat> rotations = ik::AimRotations(chain, Rotations(pose, bones));
                const std::size_t     last      = bones.size() - 1;
                rotations[last] = ik::Normalized(ik::ShortestArc(chain.source[last] - chain.source[last - 1], chain.joints[last] - chain.joints[last - 1]) *
                                                 pose.Model(bones[last]).r);
                for (std::size_t i = 0; i < bones.size(); i++) pose.SetModelRotation(bones[i], rotations[i]);
            }
            const ik::Vec3 rootMove = body.root - root0;
            if (ik::Dot(rootMove, rootMove) > 1e-8f) pose.MoveModel(rig.bodyRoot, rootMove);
            pose.Update();
        }

        for (std::size_t l = 0; l < legs.size(); l++) {
            const std::vector<hk::hkInt16>& bones = legs[l]->rigLeg->chain;
            ik::Chain                       chain = LegChain(pose, *legs[l]);
            ik::Solve(chain, Target(*legs[l], chain.joints.back()), kLimb);
            const std::vector<ik::Quat> rotations = ik::AimRotations(chain, Rotations(pose, bones));
            for (std::size_t i = 0; i + 1 < bones.size(); i++) pose.SetModelRotation(bones[i], rotations[i]);
            pose.SetModelRotation(bones.back(), ankles[l]);
            pose.Update();
        }
    }

    bool AlignToGround(const Rig& rig, hk::hkbFootIkDriver& driver, const hk::hkVector4& modelUpMS, const hk::hkVector4& modelRightMS,
                       hk::hkbGeneratorOutput& output, const hk::hkbFootIkGains& gains)
    {
        if (rig.spines.size() != 2 || static_cast<int>(rig.legs.size()) != driver.m_internalLegData.getSize() || !driver.m_isGroundPositionValid)
            return false;
        const hk::hkInt16 hindAnchor = rig.spines[0].empty() ? rig.bodyRoot : rig.spines[0].back();
        ik::Vec3          ground[2];
        int               count[2] = { 0, 0 };
        for (std::size_t i = 0; i < rig.legs.size(); i++) {
            const int side = rig.legs[i].anchor == hindAnchor ? 0 : 1;
            ground[side] += V(driver.m_internalLegData[static_cast<int>(i)].m_groundPosition);
            count[side]++;
        }
        if (!count[0] || !count[1]) return false;
        if (gains.m_alignWorldFromModelGain <= 0.0f) return true;

        hk::hkQsTransform& worldFromModel = output.accessWorldFromModel();
        const ik::Quat     rotation       = Q(worldFromModel.m_rotation);
        const ik::Vec3     up             = ik::Normalized(ik::Rotate(rotation, V(modelUpMS)));
        const ik::Vec3     right          = ik::Normalized(ik::Rotate(rotation, V(modelRightMS)));
        ik::Vec3           slope          = ground[1] * (1.0f / count[1]) - ground[0] * (1.0f / count[0]);
        slope -= right * ik::Dot(right, slope);
        if (ik::Length(slope) < 1.0f) return true;

        ik::Vec3 normal = ik::Normalized(ik::Cross(right, slope));
        if (ik::Dot(normal, up) < 0.0f) normal = -normal;
        if (std::fabs(normal.x - up.x) <= 0.001f && std::fabs(normal.y - up.y) <= 0.001f && std::fabs(normal.z - up.z) <= 0.001f) return true;

        const ik::Quat arc   = ik::ShortestArc(up, normal);
        const float    angle = 2.0f * std::acos(std::clamp(std::fabs(arc.w), 0.0f, 1.0f));
        const ik::Vec3 axis  = ik::Normalized(ik::Vec3{ arc.x, arc.y, arc.z } * (arc.w < 0.0f ? -1.0f : 1.0f));
        if (ik::Length(axis) < 0.5f) return true;
        const ik::Quat turn         = ik::AxisAngle(axis, angle * gains.m_alignWorldFromModelGain);
        driver.m_rotation30         = HkQ(turn);
        worldFromModel.m_rotation   = HkQ(ik::Normalized(turn * rotation));
        return true;
    }

}  // namespace CB::feature::footik
