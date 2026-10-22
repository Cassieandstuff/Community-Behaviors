/// @file
/// @brief hkbStateListener's default virtuals, reimplemented against vanilla (AE 1.6.1170).
/// @note Slots 3–5 are header-inline COMDAT copies in vanilla (0x140accc40–0x140accc60); defined here out of line.
#include <Behavior/StateMachine/hkbStateListener.h>

namespace CB { namespace havok
{
    void hkbStateListener::onEnter(const hkbContext& context, hkbStateMachine* sm)
    {
    }

    void hkbStateListener::onUpdate(const hkbContext& context, hkReal timestep, hkbStateMachine* sm)
    {
    }

    void hkbStateListener::onExit(const hkbContext& context, hkbStateMachine* sm)
    {
    }

    /// @note The default listener is not cloned: a cloned state machine shares none.
    hkbStateListener* hkbStateListener::clone()
    {
        return HK_NULL;
    }
} }
