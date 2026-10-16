/// @file
/// @brief hkbKeyframeBonesModifierUtils: the track work behind hkbKeyframeBonesModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0e830, in the run of out-of-line modify helpers (see
///       hkbRotateCharacterModifierUtils.h). The file name is inferred.
#pragma once

#include <Behavior/Modifier/KeyframeBones/hkbKeyframeBonesModifier.h>

namespace CB { namespace schema
{
    namespace hkbKeyframeBonesModifierUtils
    {
        /// @brief Writes the valid keyframe targets into the output's keyframe targets track (a sparse track sorted by
        ///        bone) and sets the weight of every bone of the keyframed bones list in its keyframed ragdoll bones track.
        /// @note Name provisional.
        void modify(hkbKeyframeBonesModifier& mod, hkbGeneratorOutput& inOut);
    }
} }
