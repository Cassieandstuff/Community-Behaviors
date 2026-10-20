/// @file
/// @brief hkbVariableBindingSet, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in Behavior/Node/hkbNode.h (every bindable holds one).
#include <Behavior/Node/hkbNode.h>

namespace CB { namespace havok
{
    hkbVariableBindingSet::~hkbVariableBindingSet()
    {
        if (getAllocatedSize() != 0) {
            for (int i = m_bindings.getSize() - 1; i >= 0; i--) {
                removeBinding(i);
            }
        }
    }
} }
