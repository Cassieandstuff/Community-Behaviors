/// @file
/// @brief hkbBlenderGeneratorUtils: the blender's heavy half, compiled apart (declarations only so far).
/// @note The exe names the file's main entry ("hkbBlenderGeneratorUtils::generateInternal", a monitor timer); its
///       functions sit at 0x140b07950–0x140b0c6xx, away from the blender's own TU.
#pragma once

#include <Behavior/Generator/Blender/hkbBlenderGenerator.h>

namespace CB { namespace schema
{
    struct hkbGeneratorOutput;
    struct hkbGeneratorSyncInfo;

    namespace hkbBlenderGeneratorUtils
    {
        /// @brief Blends the active children's outputs into `output`.
        /// @note 0x140b07950.
        void generateInternal(const hkbBlenderGenerator& blender, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                              const hkQsTransform* worldFromModel, const hkQsTransform* referencePose);
        /// @brief Child `childIndex`'s share of the blend: its weight clamped to [0, 1] (smoothstepped under
        ///        FLAG_SMOOTH_GENERATOR_WEIGHTS), or under FLAG_PARAMETRIC_BLEND its share of the current interval.
        /// @note Name provisional; 0x140b09320.
        hkReal computeChildWeight(const hkbBlenderGenerator& blender, int childIndex);
        /// @brief The sync point ids all `numSyncInfos` timelines have, into `syncPointIdsOut`; returns their count.
        /// @note Name provisional; 0x140b0c0d0.
        int findCommonSyncPointIds(const hkbGeneratorSyncInfo** syncInfos, int numSyncInfos, int* syncPointIdsOut);
        /// @brief Blends the children's sync infos by `weights` into `syncInfoOut` (`initSync`: start over rather than
        ///        continue from its current local time).
        /// @note Name provisional; 0x140b0c450 (shared with hkbBlendingTransitionEffect).
        void computeSyncInfo(const hkbGeneratorSyncInfo** syncInfos, int numSyncInfos, const hkReal* weights, hkReal referencePoseWeightThreshold,
                             bool initSync, hkbGeneratorSyncInfo& syncInfoOut);
    }
} }
