/// @file
/// @brief hkbModifierWrapper, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/hkbModifierWrapper.h>

namespace CB { namespace schema
{
    hkbModifierWrapper::hkbModifierWrapper()
    {
    }

    hkbModifierWrapper::hkbModifierWrapper(const hkbModifierWrapper& mod)
    :   hkbModifier(mod),
        m_modifier(mod.m_modifier)
    {
    }

    void hkbModifierWrapper::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (!(flags & FLAG_GENERATORS_ONLY)) {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node = m_modifier;
        }
    }

    bool hkbModifierWrapper::isValid(hkStringPtr& errorString) const
    {
        if (m_modifier == HK_NULL) {
            errorString = "You must provide a child modifier.";
            return false;
        }
        return true;
    }

    hkbNode* hkbModifierWrapper::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return HK_NULL;
    }

    void hkbModifierWrapper::setModifier(hkbModifier* modifier)
    {
        m_modifier = modifier;
    }
} }
