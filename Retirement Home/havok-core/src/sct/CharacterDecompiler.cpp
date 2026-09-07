#include "havok/sct/CharacterDecompiler.h"
#include "havok/sct/BehaviorDecompiler.h"   // DecompileBehaviorTree
#include "havok/sct/ProjectCompiler.h"      // ReadProject / EmitProjectYaml (project.yaml)

#include "havok/classes/Animation.h"
#include "havok/classes/Classes.h"
#include "havok/classes/gen/ClassesGen.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/model/HavokEnums.h"

#include <havok/anim/AnimationDecompiler.h>   // havok::anim::DecompileAnimation (schema-native, bytes in)
#include "havok/sct/BoneNames.h"              // BoneNameTable::names (thread the skeleton to the membrane)

#include <cstdio>
#include <exception>
#include <fstream>
#include <memory>
#include <unordered_map>

namespace havok::sct {
namespace fs = std::filesystem;

// DecompileBehaviorTree is declared in havok/sct/BehaviorDecompiler.h (defined in
// BehaviorDecompiler.cpp).

// The animation import leg moved to havok-anim (schema-native). It deserializes the raw packfile
// itself through the schema stack, so this typed character/behavior decompiler just hands it the
// bytes and adapts the result — no typed hkaAnimationContainer construction here any more.
static DecompileResult decompileAnimationBytes(const std::vector<std::uint8_t>& hkx, const fs::path& dir,
                                               const BoneNameTable* bones) {
    // Thread the (optional) skeleton bone roster so the inverse membrane decompiles each track's
    // hkaAnimationBinding bone index to its NAME; null => track<N> placeholders (identity binding).
    const auto ar = havok::anim::DecompileAnimation(hkx, dir, bones ? &bones->names : nullptr);
    return { ar.ok, ar.error, "animation" };
}

namespace {

// Emit a float so that parse(emit(v)) == v exactly (9 sig figs round-trips float).
std::string fstr(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}
std::string vec4(const Vector4& v) {
    return "[" + fstr(v.x) + ", " + fstr(v.y) + ", " + fstr(v.z) + ", " + fstr(v.w) + "]";
}
// Single-quoted YAML scalar (backslashes/brackets are literal; '' escapes ').
std::string q(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    out += "'";
    return out;
}
std::string revEnum(const std::unordered_map<std::string, long>& t, long v, const char* fb) {
    for (const auto& [k, val] : t) if (val == v) return k;
    return fb;
}
void writeText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

DecompileResult decompileCharacter(const hkbCharacterData& cd, const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::create_directories(dir / "properties", ec);

    const auto sd = cd.m_stringData;
    const auto vvs = cd.m_characterPropertyValues;
    const auto ik = cd.m_footIkDriverInfo;
    const auto mi = cd.m_mirroredSkeletonInfo;
    if (!sd) return { false, "character has no stringData", "character" };

    // ── character.yaml ──
    {
        std::string y;
        y += "packfile:\n  classversion: 8\n  contentsversion: \"hk_2010.2.0-r1\"\n\n";
        y += "character:\n";
        y += "  name: " + q(sd->m_name) + "\n";
        y += "  rig: " + q(sd->m_rigName) + "\n";
        y += "  ragdoll: " + q(sd->m_ragdollName) + "\n";
        y += "  behavior: " + q(sd->m_behaviorFilename) + "\n";
        y += "  scale: " + fstr(cd.m_scale) + "\n";
        y += "  controller:\n";
        y += "    capsuleHeight: " + fstr(cd.m_characterControllerInfo.m_capsuleHeight) + "\n";
        y += "    capsuleRadius: " + fstr(cd.m_characterControllerInfo.m_capsuleRadius) + "\n";
        y += "    collisionFilterInfo: " + std::to_string(cd.m_characterControllerInfo.m_collisionFilterInfo) + "\n";
        y += "  model:\n";
        y += "    up: " + vec4(cd.m_modelUpMS) + "\n";
        y += "    forward: " + vec4(cd.m_modelForwardMS) + "\n";
        y += "    right: " + vec4(cd.m_modelRightMS) + "\n";
        writeText(dir / "character.yaml", y);
    }

    // ── animations.txt ──
    {
        std::string t;
        for (const auto& a : sd->m_animationNames) t += a + "\n";
        writeText(dir / "animations.txt", t);
    }

    // ── properties/*.yaml + _order.txt ──
    {
        std::string order;
        const std::size_t nProps = sd->m_characterPropertyNames.size();
        for (std::size_t i = 0; i < nProps; ++i) {
            const std::string& name = sd->m_characterPropertyNames[i];
            order += name + "\n";

            std::int8_t  type = (i < cd.m_characterPropertyInfos.size()) ? cd.m_characterPropertyInfos[i].m_type : 0;
            std::int16_t role = (i < cd.m_characterPropertyInfos.size()) ? cd.m_characterPropertyInfos[i].m_role.m_role : 0;
            const bool isPointer = (type == 5 /*VARIABLE_TYPE_POINTER*/);

            std::string y;
            y += "name: " + q(name) + "\n";
            y += "type: " + revEnum(model::enums::VariableType(), type, "VARIABLE_TYPE_INT32") + "\n";
            y += "role: " + revEnum(model::enums::Role(), role, "ROLE_DEFAULT") + "\n";

            const std::int32_t word = (vvs && i < vvs->m_wordVariableValues.size()) ? vvs->m_wordVariableValues[i].m_value : 0;
            if (isPointer) {
                // Bone weights from the variant array at `word`.
                std::vector<float> weights;
                if (vvs && word >= 0 && static_cast<std::size_t>(word) < vvs->m_variantVariableValues.size()) {
                    if (auto bw = std::dynamic_pointer_cast<hkbBoneWeightArray>(vvs->m_variantVariableValues[word]))
                        weights = bw->m_boneWeights;
                }
                y += "bone_weights:\n";
                y += "  count: " + std::to_string(weights.size()) + "\n";
                std::string vals;
                for (std::size_t w = 0; w < weights.size(); ++w) { if (w) vals += ' '; vals += fstr(weights[w]); }
                y += "  values: " + q(vals) + "\n";
            } else {
                y += "initial_value: " + std::to_string(word) + "\n";
            }

            std::string safe = name;
            for (char& c : safe) if (c == '+') c = '_';
            writeText(dir / "properties" / (safe + ".yaml"), y);
        }
        writeText(dir / "properties" / "_order.txt", order);
    }

    // ── foot_ik.yaml ──
    {
        std::string y = "legs:\n";
        if (ik) {
            for (const auto& L : ik->m_legs) {
                y += "  - kneeAxisLS: " + vec4(L.m_kneeAxisLS) + "\n";
                y += "    footEndLS: " + vec4(L.m_footEndLS) + "\n";
                y += "    footPlantedAnkleHeightMS: " + fstr(L.m_footPlantedAnkleHeightMS) + "\n";
                y += "    footRaisedAnkleHeightMS: " + fstr(L.m_footRaisedAnkleHeightMS) + "\n";
                y += "    maxAnkleHeightMS: " + fstr(L.m_maxAnkleHeightMS) + "\n";
                y += "    minAnkleHeightMS: " + fstr(L.m_minAnkleHeightMS) + "\n";
                y += "    maxKneeAngleDegrees: " + fstr(L.m_maxKneeAngleDegrees) + "\n";
                y += "    minKneeAngleDegrees: " + fstr(L.m_minKneeAngleDegrees) + "\n";
                y += "    maxAnkleAngleDegrees: " + fstr(L.m_maxAnkleAngleDegrees) + "\n";
                y += "    hipIndex: " + std::to_string(L.m_hipIndex) + "\n";
                y += "    kneeIndex: " + std::to_string(L.m_kneeIndex) + "\n";
                y += "    ankleIndex: " + std::to_string(L.m_ankleIndex) + "\n";
            }
            y += "raycastDistanceUp: " + fstr(ik->m_raycastDistanceUp) + "\n";
            y += "raycastDistanceDown: " + fstr(ik->m_raycastDistanceDown) + "\n";
            y += "originalGroundHeightMS: " + fstr(ik->m_originalGroundHeightMS) + "\n";
            y += "verticalOffset: " + fstr(ik->m_verticalOffset) + "\n";
            y += "collisionFilterInfo: " + std::to_string(ik->m_collisionFilterInfo) + "\n";
            y += "forwardAlignFraction: " + fstr(ik->m_forwardAlignFraction) + "\n";
            y += "sidewaysAlignFraction: " + fstr(ik->m_sidewaysAlignFraction) + "\n";
            y += "sidewaysSampleWidth: " + fstr(ik->m_sidewaysSampleWidth) + "\n";
            y += std::string("lockFeetWhenPlanted: ") + (ik->m_lockFeetWhenPlanted ? "true" : "false") + "\n";
            y += std::string("useCharacterUpVector: ") + (ik->m_useCharacterUpVector ? "true" : "false") + "\n";
            y += std::string("isQuadrupedNarrow: ") + (ik->m_isQuadrupedNarrow ? "true" : "false") + "\n";
        }
        writeText(dir / "foot_ik.yaml", y);
    }

    // ── mirror.yaml ──
    {
        std::string y;
        if (mi) {
            y += "mirrorAxis: " + vec4(mi->m_mirrorAxis) + "\n";
            y += "bonePairMap:\n";
            y += "  count: " + std::to_string(mi->m_bonePairMap.size()) + "\n";
            std::string vals;
            for (std::size_t i = 0; i < mi->m_bonePairMap.size(); ++i) { if (i) vals += ' '; vals += std::to_string(mi->m_bonePairMap[i]); }
            y += "  values: " + q(vals) + "\n";
        }
        writeText(dir / "mirror.yaml", y);
    }

    return { true, "", "character" };
}

} // namespace

DecompileResult DecompileToDir(const std::vector<std::uint8_t>& hkx, const fs::path& outDir,
                               const BoneNameTable* bones) {
    try {
        // Animation-first, via targeted construction. A real (game/mod) animation
        // .hkx bundles an hkMemoryResourceContainer as a second root variant, and
        // the full-graph Deserialize below throws on it (that class is not ported).
        // Constructing just the animation objects off the virtual-fixup table —
        // as the editor's AnimationImport does — sidesteps the whole graph walk.
        {
            PackFileDeserializer des;
            BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, hkx);
            des.DeserializePartially(br);
            bool hasSpline = false, hasProject = false;
            for (const auto& [off, cls] : des.ListObjects()) {
                if (cls == "hkaSplineCompressedAnimation") hasSpline = true;
                if (cls == "hkbProjectData")               hasProject = true;
            }
            // Project-first: a *project.hkx root is hkbProjectData (points at the
            // character files, carries no behavior graph). Extract the spec and emit
            // project.yaml — compile round-trips it via ParseProjectYaml -> BuildProject.
            if (hasProject) {
                const auto pr = ReadProject(hkx);
                if (!pr.ok) return { false, "project read failed: " + pr.error, "project" };
                std::error_code pec;
                fs::create_directories(outDir, pec);
                writeText(outDir / "project.yaml", EmitProjectYaml(pr.spec));
                return { true, "", "project" };
            }
            if (hasSpline) {
                // havok-anim deserializes the packfile itself (schema path handles the
                // hkMemoryResourceContainer second variant that the typed graph walk can't).
                return decompileAnimationBytes(hkx, outDir, bones);
            }
        }

        // Character / behavior: full graph walk (all their classes are ported).
        PackFileDeserializer des;
        BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, hkx);
        auto root = std::dynamic_pointer_cast<hkRootLevelContainer>(des.Deserialize(br));
        if (!root || root->m_namedVariants.empty())
            return { false, "not a valid packfile root (no named variants)", "" };

        auto var = root->m_namedVariants[0].m_variant;
        if (auto cd = std::dynamic_pointer_cast<hkbCharacterData>(var))
            return decompileCharacter(*cd, outDir);   // name/content-keyed — no node #NNNN
        if (auto bg = std::dynamic_pointer_cast<hkbBehaviorGraph>(var))
            // NOTE: encounter-order ids. A behavior whose runtime deltas reference tagfile
            // #NNNN must instead be decompiled through the tagfile-id oracle (ConvertPatch
            // with the matching template XML — see `vanbase`), NOT this path, or the base
            // and the deltas end up in different id spaces (the horse-behavior crash).
            return DecompileBehaviorTree(bg, outDir, /*stableIds*/ nullptr, /*outIds*/ nullptr, bones);
        if (std::dynamic_pointer_cast<hkaAnimationContainer>(var))
            return decompileAnimationBytes(hkx, outDir, bones);
        return { false, "unrecognized root variant (not character / behavior / animation)", "unknown" };
    } catch (const std::exception& e) {
        return { false, std::string("deserialize failed: ") + e.what(), "" };
    }
}

} // namespace havok::sct
