/// @file
/// @brief hkbPoweredRagdollControlsModifierUtils: the track work behind hkbPoweredRagdollControlsModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0e020, in the run of out-of-line modify helpers (see
///       hkbRotateCharacterModifierUtils.h). The file name is inferred.
#pragma once

#include <Behavior/Modifier/PoweredRagdollControls/hkbPoweredRagdollControlsModifier.h>

namespace CB { namespace schema
{
    namespace hkbPoweredRagdollControlsModifierUtils
    {
        /// @brief Writes the modifier's controls into the output's powered ragdoll tracks.
        /// @note Name provisional; modify tail-jumps to it. Not reimplemented yet.
        void modify(hkbPoweredRagdollControlsModifier& mod, hkbGeneratorOutput& inOut);
    }
} }
