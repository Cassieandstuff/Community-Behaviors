/// @file
/// @brief hkbAttributeModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Attribute/hkbAttributeModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/Attribute/hkbAttributeModifierUtils.h>

namespace CB { namespace havok
{
    hkbAttributeModifier::hkbAttributeModifier(const hkbAttributeModifier& mod)
    :   hkbModifier(mod)
    {
        m_assignments = mod.m_assignments;
    }

    hkbNode* hkbAttributeModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet == HK_NULL) {
            hkbAttributeModifier* self = const_cast<hkbAttributeModifier*>(this);
            self->m_cloneState         = CLONE_STATE_SHARABLE;
            addReference();
            return self;
        }
        return new hkbAttributeModifier(*this);
    }

    /// @note The ids are translated from this node's behavior graph to the character's root graph.
    void hkbAttributeModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkbAttributeModifierUtils::modify(*this, inOut, context.m_behavior->m_attributeIdMap, context.m_character->m_behaviorGraph->m_attributeIdMap);
    }
} }
