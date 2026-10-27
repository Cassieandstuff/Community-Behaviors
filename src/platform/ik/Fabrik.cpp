#include "PCH.h"

#include <platform/ik/Fabrik.h>

#include <algorithm>
#include <cmath>

namespace CB::platform::ik {

    namespace {

        /// @brief Below this sine of its bend a joint is too straight to show its axis (about 2 degrees).
        constexpr float kStraightSine = 0.035f;

        /// @brief Any unit axis across `bone`.
        Vec3 AcrossOf(Vec3 bone)
        {
            Vec3 axis = Cross(bone, { 1.0f, 0.0f, 0.0f });
            if (Dot(axis, axis) < 1e-6f) axis = Cross(bone, { 0.0f, 1.0f, 0.0f });
            return Normalized(axis, { 0.0f, 0.0f, 1.0f });
        }

        /// @brief The free bone's direction once the hinge between it and the placed bone is applied: put back into the
        ///        hinge's plane and its bend clamped.
        /// @param placedIsParent True going out (the parent bone is placed, the child is free), false going back.
        /// @param frame The chain's swing this pass (Chain::frame), which carries every hinge axis.
        /// @param sourceFree The free bone's direction in the source pose (unit).
        /// @note  Clamping in both passes matters: with the pass back unconstrained, a straight joint the pass back
        ///        keeps flipping and the pass out keeps clamping settles short of the target (a 5-bone leg stalled 9.8
        ///        units off).
        Vec3 ApplyHinge(const Hinge& hinge, bool placedIsParent, Quat frame, Vec3 sourceFree, Vec3 placed, Vec3 free)
        {
            if (Dot(hinge.axis, hinge.axis) < 0.25f) {
                const float fold = std::acos(std::clamp(Dot(placed, free), -1.0f, 1.0f));
                if (fold <= hinge.maxBend) return free;
                return Rotate(AxisAngle(Normalized(Cross(placed, free), AcrossOf(placed)), hinge.maxBend), placed);
            }
            Vec3 axis = Rotate(frame, hinge.axis);
            axis      = Normalized(axis - placed * Dot(placed, axis), AcrossOf(placed));

            Vec3 inPlane = free - axis * Dot(axis, free);
            if (Length(inPlane) < 1e-4f) inPlane = Rotate(frame, sourceFree);
            inPlane = Normalized(inPlane, placed);

            const float sign = placedIsParent ? 1.0f : -1.0f;
            const float bend = std::atan2(sign * Dot(axis, Cross(placed, inPlane)), Dot(placed, inPlane));
            return Rotate(AxisAngle(axis, sign * std::clamp(bend, hinge.minBend, hinge.maxBend)), placed);
        }

    }  // namespace

    Chain MakeChain(std::span<const Vec3> joints)
    {
        Chain chain;
        chain.joints.assign(joints.begin(), joints.end());
        chain.source = chain.joints;
        const std::size_t n = joints.size();
        if (n < 2) return chain;
        for (std::size_t i = 0; i + 1 < n; i++) chain.lengths.push_back(Length(joints[i + 1] - joints[i]));

        Vec3 plane;
        for (std::size_t i = 1; i + 1 < n; i++) {
            const Vec3 c = Cross(Normalized(joints[i] - joints[i - 1]), Normalized(joints[i + 1] - joints[i]));
            if (Length(c) > kStraightSine) {
                plane = Normalized(c);
                break;
            }
        }
        for (std::size_t i = 1; i + 1 < n; i++) {
            const Vec3 parent = Normalized(joints[i] - joints[i - 1]);
            const Vec3 c      = Cross(parent, Normalized(joints[i + 1] - joints[i]));
            if (Length(c) > kStraightSine) {
                chain.hinges.push_back({ Normalized(c), 0.0f, kDefaultMaxBend });
            } else {
                const Vec3 across = Normalized(plane - parent * Dot(parent, plane));
                chain.hinges.push_back({ across, Dot(across, across) > 0.0f ? -kDefaultMaxBend : 0.0f, kDefaultMaxBend });
            }
        }
        return chain;
    }

    Vec3 ReachBack(Chain& chain, Vec3 target)
    {
        std::vector<Vec3>& p = chain.joints;
        const std::size_t  n = p.size();
        if (n < 2) return n ? p[0] : target;
        chain.frame = ShortestArc(chain.source[n - 1] - chain.source[0], target - p[0]);
        p[n - 1]    = target;
        for (std::size_t i = n - 1; i-- > 0;) {
            const Vec3 sourceBone = Normalized(chain.source[i + 1] - chain.source[i]);
            Vec3       dir        = Normalized(p[i + 1] - p[i], Rotate(chain.frame, sourceBone));
            if (i + 2 == n && chain.holdEndBone) {
                dir = Rotate(chain.frame, sourceBone);
            } else if (i + 2 < n && i < chain.hinges.size()) {
                const Vec3 child =
                    Normalized(p[i + 2] - p[i + 1], Rotate(chain.frame, Normalized(chain.source[i + 2] - chain.source[i + 1])));
                dir              = ApplyHinge(chain.hinges[i], false, chain.frame, sourceBone, child, dir);
            }
            p[i] = p[i + 1] - dir * chain.lengths[i];
        }
        return p[0];
    }

    void ReachOut(Chain& chain, Vec3 base)
    {
        std::vector<Vec3>& p = chain.joints;
        const std::size_t  n = p.size();
        if (n == 0) return;
        p[0] = base;
        for (std::size_t i = 0; i + 1 < n; i++) {
            const Vec3 sourceChild = Normalized(chain.source[i + 1] - chain.source[i]);
            Vec3       dir         = Normalized(p[i + 1] - p[i], Rotate(chain.frame, sourceChild));
            if (i >= 1 && i - 1 < chain.hinges.size()) {
                const Vec3 parent = Normalized(p[i] - p[i - 1], Rotate(chain.frame, Normalized(chain.source[i] - chain.source[i - 1])));
                dir               = ApplyHinge(chain.hinges[i - 1], true, chain.frame, sourceChild, parent, dir);
            }
            p[i + 1] = p[i] + dir * chain.lengths[i];
        }
    }

    Result Solve(Chain& chain, Vec3 target, const Params& params)
    {
        Result r;
        if (chain.joints.size() < 2) return r;
        const Vec3 base = chain.joints.front();
        for (; r.iterations < params.maxIterations; r.iterations++) {
            if (Length(chain.joints.back() - target) <= params.tolerance) break;
            ReachBack(chain, target);
            ReachOut(chain, base);
        }
        r.error = Length(chain.joints.back() - target);
        return r;
    }

    std::vector<Quat> AimRotations(const Chain& chain, std::span<const Quat> sourceRotations)
    {
        std::vector<Quat> out(sourceRotations.begin(), sourceRotations.end());
        const std::size_t n = chain.joints.size();
        if (out.size() != n) return out;
        for (std::size_t i = 0; i + 1 < n; i++) {
            const Quat swing = ShortestArc(chain.source[i + 1] - chain.source[i], chain.joints[i + 1] - chain.joints[i]);
            out[i]           = Normalized(swing * sourceRotations[i]);
        }
        return out;
    }

    float SolveBody(Body& body, const BodyParams& params)
    {
        const std::size_t numAnchors = body.spines.size();
        const auto        hangs      = [&](const Limb& limb) { return limb.anchor < numAnchors && limb.chain.joints.size() >= 2; };
        const auto        worstError = [&] {
            float worst = 0.0f;
            for (const Limb& limb : body.limbs)
                if (!limb.chain.joints.empty()) worst = std::max(worst, Length(limb.chain.joints.back() - limb.target));
            return worst;
        };

        std::vector<Vec3> want(numAnchors), moved(numAnchors);
        std::vector<int>  pulls(numAnchors);
        for (const Limb& limb : body.limbs) {
            if (!hangs(limb)) continue;
            Chain reach = limb.chain;
            want[limb.anchor] += ReachBack(reach, limb.target) - limb.chain.joints.front();
            pulls[limb.anchor]++;
        }
        for (std::size_t a = 0; a < numAnchors; a++)
            if (pulls[a]) want[a] = want[a] * (params.stiffness / static_cast<float>(pulls[a]));

        Vec3 rootMove, pulledSum;
        int  rigid = 0, pulled = 0;
        for (std::size_t a = 0; a < numAnchors; a++) {
            if (!pulls[a]) continue;
            pulledSum += want[a];
            pulled++;
            if (body.spines[a].joints.size() <= 1) {
                rootMove += want[a];
                rigid++;
            }
        }
        if (rigid) rootMove = rootMove * (1.0f / static_cast<float>(rigid));
        else if (pulled) rootMove = pulledSum * (1.0f / static_cast<float>(pulled));

        body.root += rootMove;
        for (std::size_t a = 0; a < numAnchors; a++) {
            Chain& spine = body.spines[a];
            for (Vec3& joint : spine.joints) joint += rootMove;
            moved[a] = rootMove;
            if (spine.joints.size() < 2 || !pulls[a]) continue;
            const Vec3 anchor = spine.joints.back();
            Solve(spine, anchor + (want[a] - rootMove), params.spine);
            moved[a] += spine.joints.back() - anchor;
        }

        for (Limb& limb : body.limbs) {
            if (limb.chain.joints.size() < 2) continue;
            if (limb.anchor < numAnchors)
                for (Vec3& joint : limb.chain.joints) joint += moved[limb.anchor];
            Solve(limb.chain, limb.target, params.limb);
        }
        return worstError();
    }

}  // namespace CB::platform::ik
