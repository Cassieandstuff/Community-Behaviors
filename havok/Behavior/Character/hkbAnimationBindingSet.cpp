/// @file
/// @brief hkbAnimationBindingSet and hkbAnimationBindingWithTriggers, reimplemented against vanilla (AE 1.6.1170).
/// @note Both classes are declared in hkbCharacter.h.
#include <Behavior/Character/hkbCharacter.h>

namespace CB { namespace havok
{
    /// @note The binding is not released: vanilla only nulls the pointer before the members are destroyed.
    hkbAnimationBindingWithTriggers::~hkbAnimationBindingWithTriggers()
    {
        m_binding = HK_NULL;
    }

    hkbAnimationBindingSet::~hkbAnimationBindingSet()
    {
        clear();
    }

    void hkbAnimationBindingSet::clear()
    {
        const int numBindings = m_bindings.getSize();
        for (int i = 0; i < numBindings; i++) {
            if (m_bindings[i]) {
                m_bindings[i]->removeReference();
            }
        }
        m_bindings.clear();
    }
} }
