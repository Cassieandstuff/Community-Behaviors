/// @file
/// @brief hkbTestStateChooser, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/StateMachine/hkbTestStateChooser.h>

namespace CB { namespace havok
{
    hkbTestStateChooser::hkbTestStateChooser(hkFinishLoadedObjectFlag flag) : hkbStateChooser(flag), m_string(flag)
    {
    }

    int hkbTestStateChooser::chooseStateId(const hkbContext& context, hkbStateMachine& sm)
    {
        return 0;
    }
} }
