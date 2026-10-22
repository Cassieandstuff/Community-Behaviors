/// @file
/// @brief hkbComputeDirectionModifierUtils: the computation behind hkbComputeDirectionModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0ce80, in the run of out-of-line modify helpers of a
///       dozen node classes (see hkbRotateCharacterModifierUtils.h). The file name is inferred.
#pragma once

#include <Behavior/Modifier/ComputeDirection/hkbComputeDirectionModifier.h>

namespace CB { namespace havok
{
    namespace hkbComputeDirectionModifierUtils
    {
        /// @brief Brings m_pointIn into model space (raised by m_verticalOffset along `up`), sets the ground angle (its
        ///        heading about `up`, from `forward` toward `right`) and the up angle (its elevation), then applies the
        ///        reverse / project / normalize options. Skipped without a pose, or once computed when
        ///        m_computeOnlyOnce.
        /// @note Name provisional. The axes are the character data's model-space up, forward and right.
        void modify(const hkVector4& up, const hkVector4& forward, const hkVector4& right, hkbGeneratorOutput& inOut,
                    hkbComputeDirectionModifier& mod);
    }
} }
