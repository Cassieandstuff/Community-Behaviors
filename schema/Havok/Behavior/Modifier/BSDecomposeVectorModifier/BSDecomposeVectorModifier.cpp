/// @file
/// @brief BSDecomposeVectorModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSDecomposeVectorModifier/BSDecomposeVectorModifier.h>

namespace CB { namespace schema
{
    BSDecomposeVectorModifier::BSDecomposeVectorModifier()
    :   m_vector(0.0f, 0.0f, 0.0f, 0.0f),
        m_x(0.0f),
        m_y(0.0f),
        m_z(0.0f),
        m_w(0.0f)
    {
        m_userData |= 2;
    }

    BSDecomposeVectorModifier::BSDecomposeVectorModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
        m_userData |= 2;
    }

    BSDecomposeVectorModifier::BSDecomposeVectorModifier(const BSDecomposeVectorModifier& mod)
    :   hkbModifier(mod),
        m_vector(mod.m_vector),
        m_x(mod.m_x),
        m_y(mod.m_y),
        m_z(mod.m_z),
        m_w(mod.m_w)
    {
        m_userData |= 2;
    }

    void BSDecomposeVectorModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_x = m_vector(0);
        m_y = m_vector(1);
        m_z = m_vector(2);
        m_w = m_vector(3);
    }

    hkbNode* BSDecomposeVectorModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSDecomposeVectorModifier(*this);
    }
} }
