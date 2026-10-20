/// @file
/// @brief The graph-side modifier steps (hkbModifier's statics at 0x140ad4bb0–0x140ad50a0), compiled apart from the
///        modifier's own TU; reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/hkbModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>

namespace CB { namespace havok
{
    /// @note m_enable is read before the variables are pushed (a binding may change it): the copy-back happens only when
    ///       the modifier was enabled coming in.
    void hkbModifier::updateModifier(const hkbContext& context, hkbModifier* modifier, hkReal timestep, hkbNodeInfo& info)
    {
        HK_TIMER_BEGIN_LIST("SingleModifierUpdate", "CopyVariablestoMembers");
        const bool wasEnabled  = modifier->m_enable;
        const int  hasBindings = modifier->copyVariablesToMembers(context, false);
        if (modifier->m_enable) {
            HK_TIMER_SPLIT_LIST("ActivateModifier");
            activateModifier(context, modifier, info);
            HK_TIMER_SPLIT_LIST("UpdateModifier");
            modifier->update(context, timestep);
            HK_TIMER_SPLIT_LIST("CopyVariablesFromMembers");
            if (hasBindings) modifier->copyVariablesToMembers(context, true);
        } else {
            HK_TIMER_SPLIT_LIST("DeactivateModifier");
            if (deactivateModifier(context, modifier, info) && hasBindings && wasEnabled) {
                HK_TIMER_SPLIT_LIST("CopyVariablesFromMembers");
                modifier->copyVariablesToMembers(context, true);
            }
        }
        HK_TIMER_END_LIST();
    }

    void hkbModifier::activateModifier(const hkbContext& context, hkbModifier* modifier, hkbNodeInfo& info)
    {
        if (!info.m_isActive) {
            modifier->activate(context);
            info.m_isActive = true;
            hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
            rootBehavior->m_updateActiveNodes = (info.m_numChildren > 0) | rootBehavior->m_updateActiveNodes;
        }
    }

    bool hkbModifier::deactivateModifier(const hkbContext& context, hkbModifier* modifier, hkbNodeInfo& info)
    {
        if (info.m_isActive) {
            modifier->deactivate(context);
            info.m_isActive = false;
            hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
            rootBehavior->m_updateActiveNodes = (info.m_numChildren > 0) | rootBehavior->m_updateActiveNodes;
            return true;
        }
        return false;
    }
} }
