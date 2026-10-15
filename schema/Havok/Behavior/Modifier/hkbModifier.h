/// @file
/// @brief hkbModifier: a node that modifies its child's output.
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace schema
{
    struct hkbGeneratorOutput;
    struct hkbNodeInfo;
    extern const hkClass hkbModifierClass;

    /// @note Sized 0x50: derived members start after the base's tail padding. The vtable is hkbNode's 0–22 plus 23–24
    ///       (the timer modifier's has 25 slots).
    class hkbModifier : public hkbNode
    {
    public:
        hkBool m_enable;   ///< +0x48

        HK_FORCE_INLINE hkbModifier() : m_enable(true) {}
        HK_FORCE_INLINE hkbModifier(hkFinishLoadedObjectFlag flag) : hkbNode(flag) {}
        /// @note Inline, and its vtable store survives in the clone copies: the m_enable read could alias it.
        HK_FORCE_INLINE hkbModifier(const hkbModifier& mod) : hkbNode(mod), m_enable(mod.m_enable) {}

        /// @brief Modifies the child's output in place.
        virtual void modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
        /// @brief Takes the sync info of the generator this modifier wraps.
        /// @note Name provisional. hkbModifier's is empty; hkbModifierGenerator's updateSync (0x140ae9b30) and
        ///       hkbModifierList's (0x140af9460) forward their node info to it, and only for a BSTweenerModifier
        ///       (whose override reads m_syncInfo.m_baseFrequency).
        virtual void updateSync(const hkbContext& context, hkbNodeInfo& info);      ///< slot 24
    };
} }
