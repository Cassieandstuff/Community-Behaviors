/// @file
/// @brief BSIsActiveModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSIsActiveModifier/BSIsActiveModifier.h>

namespace CB { namespace havok
{
    BSIsActiveModifier::BSIsActiveModifier()
    :   m_bIsActive0(false),
        m_bInvertActive0(false),
        m_bIsActive1(false),
        m_bInvertActive1(false),
        m_bIsActive2(false),
        m_bInvertActive2(false),
        m_bIsActive3(false),
        m_bInvertActive3(false),
        m_bIsActive4(false),
        m_bInvertActive4(false)
    {
        m_userData |= 2;
    }

    BSIsActiveModifier::BSIsActiveModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
        m_userData |= 2;
    }

    BSIsActiveModifier::BSIsActiveModifier(const BSIsActiveModifier& mod)
    :   hkbModifier(mod),
        m_bIsActive0(mod.m_bIsActive0),
        m_bInvertActive0(mod.m_bInvertActive0),
        m_bIsActive1(mod.m_bIsActive1),
        m_bInvertActive1(mod.m_bInvertActive1),
        m_bIsActive2(mod.m_bIsActive2),
        m_bInvertActive2(mod.m_bInvertActive2),
        m_bIsActive3(mod.m_bIsActive3),
        m_bInvertActive3(mod.m_bInvertActive3),
        m_bIsActive4(mod.m_bIsActive4),
        m_bInvertActive4(mod.m_bInvertActive4)
    {
        m_userData |= 2;
    }

    void BSIsActiveModifier::activate(const hkbContext& context)
    {
        m_bIsActive0 = !m_bInvertActive0;
        m_bIsActive1 = !m_bInvertActive1;
        m_bIsActive2 = !m_bInvertActive2;
        m_bIsActive3 = !m_bInvertActive3;
        m_bIsActive4 = !m_bInvertActive4;
    }

    void BSIsActiveModifier::update(const hkbContext& context, hkReal timestep)
    {
    }

    void BSIsActiveModifier::deactivate(const hkbContext& context)
    {
        m_bIsActive0 = m_bInvertActive0;
        m_bIsActive1 = m_bInvertActive1;
        m_bIsActive2 = m_bInvertActive2;
        m_bIsActive3 = m_bInvertActive3;
        m_bIsActive4 = m_bInvertActive4;
    }

    bool BSIsActiveModifier::isValid(hkStringPtr& errorString) const
    {
        return true;
    }

    hkbNode* BSIsActiveModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSIsActiveModifier(*this);
    }
} }
