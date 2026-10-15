/// @file
/// @brief BSBoneSwitchGeneratorUtils: the pose work behind BSBoneSwitchGenerator::generate.
/// @note Compiled apart from the class in vanilla (0x140ba0b00, beside BSSynchronizedClipGeneratorUtils). File name
///       provisional, after the `<Class>Utils` convention.
#pragma once

#include <Behavior/Generator/BSBoneSwitch/BSBoneSwitchGenerator.h>

namespace CB { namespace schema
{
    namespace BSBoneSwitchGeneratorUtils
    {
        /// @brief Copies, for every child, the translation and rotation of each bone it weights positively and has
        ///        sampled into `output`'s pose, marking those bones sampled; once anything was copied, a pose that is not
        ///        yet marked whole gets its whole bit from whether every bone is now sampled.
        /// @note Name provisional. Child i's output is activeChildrenOutput[i + 1] (0 is the default generator's).
        ///       Scale is never copied.
        void generate(BSBoneSwitchGenerator* gen, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output);
    }
} }
