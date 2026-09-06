#pragma once
// CharacterDefs — the character.yaml content model (port of HKBuild's
// Models/{CharacterDef,PropertyDef,FootIkDef,MirrorDef,SkeletonDef}.cs). A
// character source dir carries character.yaml + animations.txt + properties/*
// + foot_ik.yaml + mirror.yaml, plus skeleton.yaml (bone names, for resolving
// named bone-weight / bone-pair maps). CharacterBuilder turns this into an
// hkbCharacterData object graph.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace havok::model {

struct CharControllerDef {
    float capsuleHeight = 0.f;
    float capsuleRadius = 0.f;
    int   collisionFilterInfo = 0;
};

struct ModelAxesDef {
    std::array<float, 4> up{0.f, 0.f, 1.f, 0.f};
    std::array<float, 4> forward{0.f, 1.f, 0.f, 0.f};
    std::array<float, 4> right{1.f, 0.f, 0.f, 0.f};
};

struct CharacterDef {
    std::string       name, rig, ragdoll, behavior;
    float             scale = 1.f;
    CharControllerDef controller;
    ModelAxesDef      model;
};

struct CharBoneWeightsDef {
    int                                              count = 0;
    std::string                                      values;   // space-separated floats
    std::vector<std::pair<std::string, std::string>> named;    // bone name -> weight
    std::optional<int>                               boneCount;
    bool hasData() const { return !named.empty() || (count > 0 && !values.empty()); }
};

struct CharPropertyDef {
    std::string                        name;
    std::string                        type;
    std::string                        role = "ROLE_DEFAULT";
    std::optional<long>                initialValue;
    std::optional<CharBoneWeightsDef>  boneWeights;
    bool isPointer() const { return type == "VARIABLE_TYPE_POINTER"; }
};

struct FootIkLegDef {
    std::array<float, 4> kneeAxisLS{};
    std::array<float, 4> footEndLS{};
    float footPlantedAnkleHeightMS = 0.f, footRaisedAnkleHeightMS = 0.f;
    float maxAnkleHeightMS = 0.f, minAnkleHeightMS = 0.f;
    float maxKneeAngleDegrees = 0.f, minKneeAngleDegrees = 0.f, maxAnkleAngleDegrees = 0.f;
    int   hipIndex = 0, kneeIndex = 0, ankleIndex = 0;
};

struct FootIkDef {
    std::vector<FootIkLegDef> legs;
    float raycastDistanceUp = 0.f, raycastDistanceDown = 0.f;
    float originalGroundHeightMS = 0.f, verticalOffset = 0.f;
    int   collisionFilterInfo = 0;
    float forwardAlignFraction = 0.f, sidewaysAlignFraction = 0.f, sidewaysSampleWidth = 0.f;
    bool  lockFeetWhenPlanted = false, useCharacterUpVector = false, isQuadrupedNarrow = false;
};

struct BonePairMapDef {
    int                                              count = 0;
    std::string                                      values;
    std::vector<std::pair<std::string, std::string>> named;   // from bone -> to bone
    bool isNamed() const { return !named.empty(); }
};

struct CharMirrorDef {
    std::array<float, 4> mirrorAxis{1.f, 0.f, 0.f, 0.f};
    BonePairMapDef       bonePairMap;
};

// Everything CharacterBuilder needs, loaded from the source directory.
struct CharacterData {
    CharacterDef                 character;
    std::vector<std::string>     animations;
    std::vector<CharPropertyDef> properties;
    FootIkDef                    footIk;
    CharMirrorDef                mirror;
    std::vector<std::string>     boneNames;   // empty if no skeleton.yaml found
};

} // namespace havok::model
