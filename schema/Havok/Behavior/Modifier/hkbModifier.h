/// @file
/// @brief hkbModifier: a node that modifies its child's output.
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace schema
{
    /// @note Sized 0x50: derived members start after the base's tail padding.
    struct hkbModifier : hkbNode
    {
        hkBool m_enable;   ///< +0x48
    };
} }
