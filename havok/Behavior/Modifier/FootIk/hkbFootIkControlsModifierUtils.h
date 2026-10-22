/// @file
/// @brief hkbFootIkControlsModifierUtils: the track write and leg copy behind hkbFootIkControlsModifier.
/// @note A file of its own in vanilla: both functions sit at 0x140b0d4c0, in the run of out-of-line modify helpers of a
///       dozen node classes (see hkbRotateCharacterModifierUtils.h). The file name is inferred.
#pragma once

#include <Behavior/Modifier/FootIk/hkbFootIkControlsModifier.h>

namespace CB { namespace havok
{
    namespace hkbFootIkControlsModifierUtils
    {
        /// @brief Writes the control data into the output's foot IK controls track (turning it fully on when it has
        ///        room), then copies the driver's legs.
        /// @note Name provisional. Nothing happens when the output has no foot IK controls track.
        void modify(hkbFootIkControlsModifier& mod, hkbGeneratorOutput& output, const hkbFootIkDriver::InternalLegData* legs, int numLegs);
        /// @brief Copies each of the driver's `numLegs` legs' ground state into the modifier's; legs past them are cleared.
        /// @note Name provisional.
        void copyLegs(hkbFootIkControlsModifier& mod, const hkbFootIkDriver::InternalLegData* legs, int numLegs);
    }
} }
