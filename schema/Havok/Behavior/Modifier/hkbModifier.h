/// @file
/// @brief hkbModifier: a node that modifies its child's output.
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace schema
{
    /// @note Sized 0x50: derived members start after the base's tail padding.
    class hkbModifier : public hkbNode
    {
    public:
        hkBool m_enable;   ///< +0x48

        /// @note Inline, and its vtable store survives in the clone copies: the m_enable read could alias it.
        HK_FORCE_INLINE hkbModifier(const hkbModifier& mod) : hkbNode(mod), m_enable(mod.m_enable) {}
    };
} }
