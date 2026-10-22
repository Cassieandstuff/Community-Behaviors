/// @file
/// @brief hkbDampingModifierUtils: the controller step behind hkbDampingModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0d3f0, in the run of out-of-line modify helpers of a
///       dozen node classes (see hkbRotateCharacterModifierUtils.h). The file name is inferred.
#pragma once

#include <Behavior/Modifier/Damping/hkbDampingModifier.h>

namespace CB { namespace havok
{
    namespace hkbDampingModifierUtils
    {
        /// @brief One PID step of each enabled channel: the damped value moves by kP x error + kI x accumulated error
        ///        + kD x change of error.
        /// @note Name provisional.
        void modify(hkbDampingModifier& mod);
    }
} }
