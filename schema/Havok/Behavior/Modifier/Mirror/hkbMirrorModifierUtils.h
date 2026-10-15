/// @file
/// @brief hkbMirrorModifierUtils: the pose and motion work behind hkbMirrorModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0dac0, in the run of out-of-line modify helpers of a
///       dozen node classes (see hkbRotateCharacterModifierUtils.h). The file name is inferred.
#pragma once

#include <Behavior/Modifier/Mirror/hkbMirrorModifier.h>

namespace CB { namespace schema
{
    namespace hkbMirrorModifierUtils
    {
        /// @brief Mirrors the output pose (filled in densely from `referencePose` first, each bone mirrored and moved to
        ///        its partner's slot) and the output world-from-model about the character's `worldFromModel`.
        /// @note Name provisional.
        void modify(const hkbMirrorModifier& mod, const hkaMirroredSkeleton& mirroredSkeleton, const hkQsTransform* referencePose,
                    const hkQsTransform& worldFromModel, hkbGeneratorOutput& inOut);
    }
} }
