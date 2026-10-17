/// @file
/// @brief BSModifyOnceModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSModifyOnceModifier/BSModifyOnceModifier.h>

namespace CB { namespace schema
{
    BSModifyOnceModifier::BSModifyOnceModifier()
    :   m_pOnActivateModifier(HK_NULL),
        m_pOnDeactivateModifier(HK_NULL)
    {
        m_userData |= 3;
    }

    BSModifyOnceModifier::BSModifyOnceModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
        m_userData |= 3;
    }

    BSModifyOnceModifier::BSModifyOnceModifier(const BSModifyOnceModifier& mod, hkbBehaviorGraph& rootBehavior)
    :   hkbModifier(mod),
        m_pOnActivateModifier(mod.m_pOnActivateModifier ? static_cast<hkbModifier*>(mod.m_pOnActivateModifier->cloneNode(rootBehavior)) : HK_NULL),
        m_pOnDeactivateModifier(mod.m_pOnDeactivateModifier ? static_cast<hkbModifier*>(mod.m_pOnDeactivateModifier->cloneNode(rootBehavior)) : HK_NULL)
    {
        m_userData |= 3;
    }

    BSModifyOnceModifier::~BSModifyOnceModifier()
    {
        if (m_pOnActivateModifier && m_pOnActivateModifier->m_cloneState == CLONE_STATE_CLONE) delete m_pOnActivateModifier;
        if (m_pOnDeactivateModifier && m_pOnDeactivateModifier->m_cloneState == CLONE_STATE_CLONE) delete m_pOnDeactivateModifier;
    }

    /// @note The modifier runs a zero-length cycle (update with timestep 0); only an enabled node runs it on activate.
    void BSModifyOnceModifier::activate(const hkbContext& context)
    {
        if (m_enable && m_pOnActivateModifier) {
            m_pOnActivateModifier->activate(context);
            m_pOnActivateModifier->update(context, 0.0f);
            m_pOnActivateModifier->deactivate(context);
        }
    }

    /// @note Unlike activate, runs whether or not the node is enabled.
    void BSModifyOnceModifier::deactivate(const hkbContext& context)
    {
        if (m_pOnDeactivateModifier) {
            m_pOnDeactivateModifier->activate(context);
            m_pOnDeactivateModifier->update(context, 0.0f);
            m_pOnDeactivateModifier->deactivate(context);
        }
    }

    int BSModifyOnceModifier::getMaxNumChildren(GetChildrenFlags flags)
    {
        if (m_cloneState == CLONE_STATE_CLONE || (flags & FLAG_ACTIVE_ONLY)) return 0;
        return 2;
    }

    /// @note A clone reports no children: its modifiers never run as graph nodes, only through activate/deactivate.
    void BSModifyOnceModifier::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (m_cloneState == CLONE_STATE_CLONE || (flags & FLAG_ACTIVE_ONLY)) {
            childrenInfo.m_childInfos.clear();
            return;
        }
        if (m_pOnActivateModifier) childrenInfo.m_childInfos.expandOne().m_node = m_pOnActivateModifier;
        if (m_pOnDeactivateModifier) childrenInfo.m_childInfos.expandOne().m_node = m_pOnDeactivateModifier;
    }

    bool BSModifyOnceModifier::isValid(hkStringPtr& errorString) const
    {
        if (!m_pOnActivateModifier && !m_pOnDeactivateModifier) errorString = "Need to provide a activate or deactivate modifier";
        return m_pOnActivateModifier || m_pOnDeactivateModifier;
    }

    void BSModifyOnceModifier::precompute(const hkbContext& context)
    {
        if (m_pOnActivateModifier) m_pOnActivateModifier->precompute(context);
        if (m_pOnDeactivateModifier) m_pOnDeactivateModifier->precompute(context);
    }

    hkbNode* BSModifyOnceModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSModifyOnceModifier(*this, rootBehavior);
    }
} }
