/// @file
/// @brief hkbModifier: a node that modifies its child's output.
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace schema
{
    struct hkbGeneratorOutput;

    /// @note Sized 0x50: derived members start after the base's tail padding. The vtable is hkbNode's 0–22 plus 23–24
    ///       (the timer modifier's has 25 slots).
    class hkbModifier : public hkbNode
    {
    public:
        hkBool m_enable;   ///< +0x48

        HK_FORCE_INLINE hkbModifier() : m_enable(true) {}
        /// @note Inline, and its vtable store survives in the clone copies: the m_enable read could alias it.
        HK_FORCE_INLINE hkbModifier(const hkbModifier& mod) : hkbNode(mod), m_enable(mod.m_enable) {}

        /// @brief Modifies the child's output in place.
        virtual void modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
        /// @note Placeholder until a class's override identifies it (Havok's is predictiveModify).
        virtual void vfn24();                                                        ///< slot 24
    };
} }
