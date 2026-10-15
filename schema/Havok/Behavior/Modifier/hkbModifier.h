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

        /// @brief Runs `modifier` (a template, or a clone) on `inOut` the way the graph does: through its clone in the
        ///        root graph, only while that clone is active, notifying the context's output listener.
        /// @note Name provisional; 0x140ad4bb0, among the graph's own functions. Every node that runs a child modifier
        ///       (hkbModifierList, hkbModifierGenerator, the modifier wrappers) calls it, and nothing else does.
        static void callModify(const hkbContext& context, hkbModifier* modifier, hkbGeneratorOutput& inOut);
        /// @brief The graph's per-frame step for one modifier: pushes its bound variables in, then activates and updates it
        ///        while it is enabled, or deactivates it once disabled, copying its members back to the variables when it
        ///        has bindings (and was enabled).
        /// @note Name provisional (its monitor list is "SingleModifierUpdate"); 0x140ad4da0, beside callModify.
        static void updateModifier(const hkbContext& context, hkbModifier* modifier, hkReal timestep, hkbNodeInfo& info);
        /// @brief Activates `modifier` unless its record says it is active, and asks the root graph to recompute its
        ///        active nodes when the modifier has children.
        /// @note Name provisional (0x140ad4ff0).
        static void activateModifier(const hkbContext& context, hkbModifier* modifier, hkbNodeInfo& info);
        /// @brief deactivate's counterpart of activateModifier.
        /// @return Whether it was active (and is now deactivated).
        /// @note Name provisional (0x140ad5050).
        static bool deactivateModifier(const hkbContext& context, hkbModifier* modifier, hkbNodeInfo& info);

        /// @brief Modifies the child's output in place.
        virtual void modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
        /// @brief Lets the modifier adjust the sync info its generator reports (`info` is that generator's node info).
        /// @note Name provisional. hkbModifier's is empty; hkbModifierGenerator::updateSync calls it on its modifier's
        ///       clone when that is an hkbModifierList or a BSTweenerModifier, and the list and the wrappers forward it to
        ///       their children. Not Havok's predictiveModify: the second argument is an hkbNodeInfo.
        virtual void updateSync(const hkbContext& context, hkbNodeInfo& info);       ///< slot 24
    };
} }
