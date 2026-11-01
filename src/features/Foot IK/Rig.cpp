#include "PCH.h"

#include "features/Foot IK/Rig.h"

#include <Common/Base/hkBase.h>
#include <Animation/Animation/Rig/hkaSkeleton.h>
#include <Behavior/Behavior/Driver/FootIk/hkbFootIkDriverInfo.h>

#include <algorithm>
#include <format>

namespace CB::feature::footik {

    namespace {


        ::hkInt16 Parent(const ::hkaSkeleton& skeleton, ::hkInt16 bone)
        {
            return skeleton.m_parentIndices[bone];
        }

        /// @brief Marks `bone` and every bone above it.
        void MarkUp(const ::hkaSkeleton& skeleton, ::hkInt16 bone, std::vector<bool>& marks)
        {
            for (; bone >= 0; bone = Parent(skeleton, bone)) marks[bone] = true;
        }

        ::hkInt16 CommonAncestor(const ::hkaSkeleton& skeleton, ::hkInt16 a, ::hkInt16 b)
        {
            std::vector<bool> above(skeleton.m_parentIndices.getSize(), false);
            MarkUp(skeleton, a, above);
            while (b >= 0 && !above[b]) b = Parent(skeleton, b);
            return b;
        }

        /// @brief The bones from just below `top` down to `bone`, parent first; all of them when `top` is not above it.
        std::vector<::hkInt16> PathBelow(const ::hkaSkeleton& skeleton, ::hkInt16 top, ::hkInt16 bone)
        {
            std::vector<::hkInt16> path;
            for (; bone >= 0 && bone != top; bone = Parent(skeleton, bone)) path.push_back(bone);
            std::reverse(path.begin(), path.end());
            return path;
        }

        std::string BoneName(const ::hkaSkeleton& skeleton, ::hkInt16 bone)
        {
            if (bone < 0) return "(none)";
            const char* name = skeleton.m_bones[bone].m_name.cString();
            return name ? name : std::format("#{}", bone);
        }

        std::string Names(const ::hkaSkeleton& skeleton, const std::vector<::hkInt16>& bones)
        {
            std::string out;
            for (const ::hkInt16 bone : bones) {
                if (!out.empty()) out += " > ";
                out += BoneName(skeleton, bone);
            }
            return out;
        }

    }  // namespace

    Rig DeriveRig(const ::hkaSkeleton& skeleton, const ::hkbFootIkDriverInfo& info)
    {
        Rig        rig;
        const int  numBones = skeleton.m_parentIndices.getSize();
        const auto valid    = [&](::hkInt16 bone) { return bone >= 0 && bone < numBones; };
        for (int i = 0; i < info.m_legs.getSize(); i++) {
            const ::hkbFootIkDriverInfo::Leg& leg = info.m_legs[i];
            if (!valid(leg.m_hipIndex) || !valid(leg.m_kneeIndex) || !valid(leg.m_ankleIndex)) {
                rig.problem = std::format("leg {}: a bone index is outside the skeleton's {} bones", i, numBones);
                return rig;
            }
        }

        for (int i = 0; i < info.m_legs.getSize(); i++) {
            std::vector<bool> others(numBones, false);
            for (int j = 0; j < info.m_legs.getSize(); j++)
                if (j != i) MarkUp(skeleton, info.m_legs[j].m_ankleIndex, others);

            const ::hkbFootIkDriverInfo::Leg& leg    = info.m_legs[i];
            ::hkInt16                         anchor = Parent(skeleton, leg.m_ankleIndex);
            while (anchor >= 0 && !others[anchor]) anchor = Parent(skeleton, anchor);
            if (anchor < 0) anchor = Parent(skeleton, leg.m_hipIndex);
            rig.legs.push_back({ PathBelow(skeleton, anchor, leg.m_ankleIndex), anchor });
        }

        std::vector<::hkInt16> anchors;
        for (const RigLeg& leg : rig.legs)
            if (leg.anchor >= 0 && std::find(anchors.begin(), anchors.end(), leg.anchor) == anchors.end()) anchors.push_back(leg.anchor);
        if (anchors.empty()) return rig;

        rig.bodyRoot = anchors.front();
        for (const ::hkInt16 anchor : anchors) rig.bodyRoot = CommonAncestor(skeleton, rig.bodyRoot, anchor);
        for (const ::hkInt16 anchor : anchors) rig.spines.push_back(PathBelow(skeleton, rig.bodyRoot, anchor));
        return rig;
    }

    std::string Describe(const Rig& rig, const ::hkaSkeleton& skeleton)
    {
        if (!rig.problem.empty()) return rig.problem;
        std::string out = std::format("body root {}", BoneName(skeleton, rig.bodyRoot));
        for (std::size_t i = 0; i < rig.legs.size(); i++)
            out += std::format("\n  leg {} @ {}: {}", i, BoneName(skeleton, rig.legs[i].anchor), Names(skeleton, rig.legs[i].chain));
        for (const std::vector<::hkInt16>& spine : rig.spines)
            if (!spine.empty()) out += std::format("\n  spine to {}: {}", BoneName(skeleton, spine.back()), Names(skeleton, spine));
        return out;
    }

}  // namespace CB::feature::footik
