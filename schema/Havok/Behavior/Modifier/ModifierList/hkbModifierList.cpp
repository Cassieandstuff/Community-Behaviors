/// @file
/// @brief hkbModifierList, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/ModifierList/hkbModifierList.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/BSTweenerModifier/BSTweenerModifier.h>

namespace CB { namespace schema
{
    hkbModifierList::hkbModifierList(const hkbModifierList& mod)
    :   hkbModifier(mod)
    {
        m_userData |= 1;
        m_modifiers = mod.m_modifiers;
        const int n = m_modifiers.getSize();
        for (int i = 0; i < n; i++) {
            m_modifiers[i]->addReference();
        }
    }

    hkbModifierList::~hkbModifierList()
    {
        const int n = m_modifiers.getSize();
        for (int i = 0; i < n; i++) {
            if (m_modifiers[i]) m_modifiers[i]->removeReference();
        }
    }

    const hkClass& hkbModifierList::staticClass()
    {
        return hkbModifierListClass;
    }

    const void* hkbModifierList::getClassType() const
    {
        return &staticClass();
    }

    int hkbModifierList::getMaxNumChildren(GetChildrenFlags flags)
    {
        if (!(flags & FLAG_GENERATORS_ONLY) && m_enable) return m_modifiers.getSize();
        return 0;
    }

    void hkbModifierList::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (!(flags & FLAG_GENERATORS_ONLY) && (m_enable || !(flags & FLAG_ACTIVE_ONLY))) {
            const int n = m_modifiers.getSize();
            childrenInfo.m_childInfos.setSize(n);
            for (int i = 0; i < n; i++) {
                childrenInfo.m_childInfos[i].m_node = m_modifiers[i];
            }
        }
    }

    bool hkbModifierList::isValid(hkStringPtr& errorString) const
    {
        for (int i = 0; i < m_modifiers.getSize(); i++) {
            if (m_modifiers[i] == HK_NULL) {
                errorString = "There is a null modifier in a modifier list.";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbModifierList::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet == HK_NULL) {
            hkbModifierList* self = const_cast<hkbModifierList*>(this);
            self->m_cloneState    = CLONE_STATE_SHARABLE;
            addReference();
            return self;
        }
        return new hkbModifierList(*this);
    }

    void hkbModifierList::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        const int n = m_modifiers.getSize();
        for (int i = 0; i < n; i++) {
            callModify(context, m_modifiers[i], inOut);
        }
    }

    void hkbModifierList::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        const int n = m_modifiers.getSize();
        for (int i = 0; i < n; i++) {
            hkbModifier* modifier = m_modifiers[i];
            if (modifier->m_enable && modifier->getClassType() == &BSTweenerModifier::staticClass()) {
                hkbModifier* clone = static_cast<hkbModifier*>(context.m_character->m_behaviorGraph->getNodeClone(m_modifiers[i]));
                clone->updateSync(context, info);
            }
        }
    }
} }
