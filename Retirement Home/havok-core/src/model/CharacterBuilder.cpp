#include "havok/model/CharacterBuilder.h"
#include "havok/model/HavokEnums.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>

namespace havok::model {
namespace {

// Sanity cap on a *computed* bone-weight array length (named / bone_count path only).
// See BehaviorBuilder for the rationale; real Skyrim skeletons stay well under this.
// The raw (count+values) path never uses it.
constexpr int kMaxBoneWeightArrayLength = 8192;

Vector4 v4(const std::array<float, 4>& a) { return Vector4{ a[0], a[1], a[2], a[3] }; }

float pf(const std::string& s) {
    if (s.empty()) return 0.f;
    try { return std::stof(s); } catch (...) { return 0.f; }
}

} // namespace

int CharacterBuilder::findBone(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(_d.boneNames.size()); i++)
        if (_d.boneNames[i] == name) return i;
    return -1;
}

std::shared_ptr<hkbBoneWeightArray>
CharacterBuilder::buildBoneWeights(const CharBoneWeightsDef& bw) {
    auto arr = std::make_shared<hkbBoneWeightArray>();
    // Named / bone_count path (OPTIONAL, largely dormant — all shipped bone-weight
    // sources are raw). A pointer property with a bone_count (even with an empty
    // `named: {}`) is a full-skeleton-length array of zeros — C# emits bone_count
    // entries, not none.
    //
    // INVARIANT ENFORCED: no wrong-length / out-of-bounds hkbBoneWeightArray can be
    // produced. A named mask requires a skeleton (existing throw); the emitted length
    // may not exceed a sane cap; and every named bone must resolve to an in-range
    // index, else we throw rather than silently skipping. (The raw branch below is the
    // primary path and is left byte-identical.)
    if (!bw.named.empty() || bw.boneCount) {
        // Named -> flat array against skeleton bone order (default 0.0).
        const int out = bw.boneCount.value_or(static_cast<int>(_d.boneNames.size()));
        if (!bw.named.empty() && _d.boneNames.empty())
            throw std::runtime_error("CharacterBuilder: named bone weights but no skeleton.yaml");
        if (out > kMaxBoneWeightArrayLength)
            throw std::runtime_error(
                "CharacterBuilder: computed bone-weight array length " + std::to_string(out) +
                " exceeds sane maximum " + std::to_string(kMaxBoneWeightArrayLength) +
                " — bad bone_count.");
        std::vector<float> w(static_cast<std::size_t>(std::max(0, out)), 0.f);
        for (const auto& [bone, weight] : bw.named) {
            const int idx = findBone(bone);
            if (idx < 0)
                throw std::runtime_error(
                    "CharacterBuilder: named bone '" + bone +
                    "' not found in skeleton — wrong or mismatched skeleton.");
            if (idx >= out)
                throw std::runtime_error(
                    "CharacterBuilder: bone '" + bone + "' index " + std::to_string(idx) +
                    " exceeds bone-weight array length " + std::to_string(out) + ".");
            w[static_cast<std::size_t>(idx)] = pf(weight);
        }
        arr->m_boneWeights = std::move(w);
    } else if (bw.count > 0 && !bw.values.empty()) {
        std::stringstream ss(bw.values);
        float f;
        while (ss >> f) arr->m_boneWeights.push_back(f);
    }
    return arr;
}

std::shared_ptr<hkRootLevelContainer> CharacterBuilder::Build() {
    const CharacterDef& ch = _d.character;

    // 1) hkbBoneWeightArray — one per POINTER property, in property order.
    std::vector<std::shared_ptr<hkbBoneWeightArray>> boneWeights;
    for (const auto& p : _d.properties) {
        if (!p.isPointer()) continue;
        boneWeights.push_back(p.boneWeights ? buildBoneWeights(*p.boneWeights)
                                            : std::make_shared<hkbBoneWeightArray>());
    }

    // 2) hkbVariableValueSet — word values (pointer -> variant index, else initial),
    //    and the bone-weight arrays as variant values.
    auto vvs = std::make_shared<hkbVariableValueSet>();
    int pointerIndex = 0;
    for (const auto& p : _d.properties) {
        hkbVariableValue vv;
        vv.m_value = p.isPointer() ? pointerIndex++
                                   : static_cast<std::int32_t>(p.initialValue.value_or(0));
        vvs->m_wordVariableValues.push_back(vv);
    }
    for (const auto& b : boneWeights)
        vvs->m_variantVariableValues.push_back(std::static_pointer_cast<hkReferencedObject>(b));

    // 3) hkbFootIkDriverInfo
    auto ik = std::make_shared<hkbFootIkDriverInfo>();
    for (const auto& L : _d.footIk.legs) {
        hkbFootIkDriverInfoLeg leg;
        leg.m_kneeAxisLS = v4(L.kneeAxisLS);
        leg.m_footEndLS  = v4(L.footEndLS);
        leg.m_footPlantedAnkleHeightMS = L.footPlantedAnkleHeightMS;
        leg.m_footRaisedAnkleHeightMS  = L.footRaisedAnkleHeightMS;
        leg.m_maxAnkleHeightMS = L.maxAnkleHeightMS;
        leg.m_minAnkleHeightMS = L.minAnkleHeightMS;
        leg.m_maxKneeAngleDegrees  = L.maxKneeAngleDegrees;
        leg.m_minKneeAngleDegrees  = L.minKneeAngleDegrees;
        leg.m_maxAnkleAngleDegrees = L.maxAnkleAngleDegrees;
        leg.m_hipIndex   = static_cast<std::int16_t>(L.hipIndex);
        leg.m_kneeIndex  = static_cast<std::int16_t>(L.kneeIndex);
        leg.m_ankleIndex = static_cast<std::int16_t>(L.ankleIndex);
        ik->m_legs.push_back(leg);
    }
    ik->m_raycastDistanceUp     = _d.footIk.raycastDistanceUp;
    ik->m_raycastDistanceDown   = _d.footIk.raycastDistanceDown;
    ik->m_originalGroundHeightMS = _d.footIk.originalGroundHeightMS;
    ik->m_verticalOffset        = _d.footIk.verticalOffset;
    ik->m_collisionFilterInfo   = static_cast<std::uint32_t>(_d.footIk.collisionFilterInfo);
    ik->m_forwardAlignFraction  = _d.footIk.forwardAlignFraction;
    ik->m_sidewaysAlignFraction = _d.footIk.sidewaysAlignFraction;
    ik->m_sidewaysSampleWidth   = _d.footIk.sidewaysSampleWidth;
    ik->m_lockFeetWhenPlanted   = _d.footIk.lockFeetWhenPlanted;
    ik->m_useCharacterUpVector  = _d.footIk.useCharacterUpVector;
    ik->m_isQuadrupedNarrow     = _d.footIk.isQuadrupedNarrow;

    // 4) hkbCharacterStringData
    auto sd = std::make_shared<hkbCharacterStringData>();
    sd->m_animationNames = _d.animations;
    for (const auto& p : _d.properties) sd->m_characterPropertyNames.push_back(p.name);
    sd->m_name            = ch.name;
    sd->m_rigName         = ch.rig;
    sd->m_ragdollName     = ch.ragdoll;
    sd->m_behaviorFilename = ch.behavior;

    // 5) hkbMirroredSkeletonInfo
    auto mi = std::make_shared<hkbMirroredSkeletonInfo>();
    mi->m_mirrorAxis = v4(_d.mirror.mirrorAxis);
    const BonePairMapDef& bp = _d.mirror.bonePairMap;
    if (bp.isNamed()) {
        if (_d.boneNames.empty())
            throw std::runtime_error("CharacterBuilder: named bone pair map but no skeleton.yaml");
        std::vector<std::int16_t> map(_d.boneNames.size());
        for (int i = 0; i < static_cast<int>(map.size()); i++) map[i] = static_cast<std::int16_t>(i);
        for (const auto& [from, to] : bp.named) {
            const int fi = findBone(from), ti = findBone(to);
            if (fi >= 0 && ti >= 0) map[static_cast<std::size_t>(fi)] = static_cast<std::int16_t>(ti);
        }
        mi->m_bonePairMap = std::move(map);
    } else if (bp.count > 0 && !bp.values.empty()) {
        std::stringstream ss(bp.values);
        int v;
        while (ss >> v) mi->m_bonePairMap.push_back(static_cast<std::int16_t>(v));
    }

    // 6) hkbCharacterData
    auto cd = std::make_shared<hkbCharacterData>();
    cd->m_characterControllerInfo.m_capsuleHeight = ch.controller.capsuleHeight;
    cd->m_characterControllerInfo.m_capsuleRadius = ch.controller.capsuleRadius;
    cd->m_characterControllerInfo.m_collisionFilterInfo =
        static_cast<std::uint32_t>(ch.controller.collisionFilterInfo);
    cd->m_modelUpMS      = v4(ch.model.up);
    cd->m_modelForwardMS = v4(ch.model.forward);
    cd->m_modelRightMS   = v4(ch.model.right);
    for (const auto& p : _d.properties) {
        hkbVariableInfo vi;
        vi.m_role.m_role  = static_cast<std::int16_t>(enums::ResolveEnum(p.role, enums::Role()));
        vi.m_role.m_flags = 0;
        vi.m_type         = static_cast<std::int8_t>(enums::ResolveEnum(p.type, enums::VariableType()));
        cd->m_characterPropertyInfos.push_back(vi);
    }
    cd->m_characterPropertyValues = vvs;
    cd->m_footIkDriverInfo        = ik;
    cd->m_stringData              = sd;
    cd->m_mirroredSkeletonInfo    = mi;
    cd->m_scale                   = ch.scale;

    // 7) hkRootLevelContainer
    auto root = std::make_shared<hkRootLevelContainer>();
    hkRootLevelContainerNamedVariant nv;
    nv.m_name      = "hkbCharacterData";
    nv.m_className = "hkbCharacterData";
    nv.m_variant   = cd;
    root->m_namedVariants.push_back(nv);
    return root;
}

} // namespace havok::model
