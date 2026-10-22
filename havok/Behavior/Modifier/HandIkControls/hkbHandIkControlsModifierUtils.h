/// @file
/// @brief hkbHandIkControlsModifierUtils: the track writes behind hkbHandIkControlsModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0d900, in the run of out-of-line modify helpers of a
///       dozen node classes (see hkbRotateCharacterModifierUtils.h); modify tail-jumps to it. The file name is inferred.
#pragma once

#include <Behavior/Modifier/HandIkControls/hkbHandIkControlsModifier.h>

namespace CB { namespace havok
{
    namespace hkbHandIkControlsModifierUtils
    {
        /// @brief Copies each enabled hand's control data into both of its hand's tracks (blendable and non-blendable)
        ///        and turns them fully on; the target position's w is 1 when the transform is on, else the position is
        ///        zeroed.
        /// @note Name provisional.
        void modify(const hkbHandIkControlsModifier& mod, hkbGeneratorOutput& output);
    }
} }
