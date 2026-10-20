/// @file
/// @brief hkbRigidBodyRagdollControlsModifierUtils: the track work behind hkbRigidBodyRagdollControlsModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0e600, in the run of out-of-line modify helpers
///       (see hkbRotateCharacterModifierUtils.h). The file name is inferred.
#pragma once

#include <Behavior/Modifier/RigidBodyRagdollControls/hkbRigidBodyRagdollControlsModifier.h>

namespace CB { namespace havok
{
    namespace hkbRigidBodyRagdollControlsModifierUtils
    {
        /// @brief Writes the modifier's control data into the output's rigid body ragdoll controls track (for its bones,
        ///        or every bone without a list) and its blend duration into the blend time track.
        /// @note Name provisional. The controls go in as a one-entry palette track: bones off the list get palette index
        ///       -1. Nothing is written when the list has more bones than the track.
        void modify(hkbRigidBodyRagdollControlsModifier& mod, hkbGeneratorOutput& inOut);
    }
} }
