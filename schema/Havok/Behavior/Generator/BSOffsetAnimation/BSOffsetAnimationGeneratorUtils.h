/// @file
/// @brief BSOffsetAnimationGeneratorUtils: the pose work behind BSOffsetAnimationGenerator::generate.
/// @note Compiled apart from the class in vanilla (0x140ba0fb0, beside BSSynchronizedClipGeneratorUtils). File name
///       provisional, after the `<Class>Utils` convention.
#pragma once

#include <Behavior/Generator/BSOffsetAnimation/BSOffsetAnimationGenerator.h>

namespace CB { namespace schema
{
    namespace BSOffsetAnimationGeneratorUtils
    {
        /// @brief Multiplies `offsets[i]` onto the pose bone `boneIndices[i]` names, for every such bone the output has
        ///        sampled; nothing while the generator's offset is zero or not set up.
        /// @note Name provisional. `activeChildrenOutput` is passed but unused.
        void applyOffsets(BSOffsetAnimationGenerator* gen, const hkArray<hkQsTransform>& offsets, const hkArray<hkInt16>& boneIndices,
                          const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output);
    }
} }
