/// @file
/// @brief hkbStateChooser: picks the state a state machine starts in (hkbStateMachine::START_STATE_MODE_CHOOSER).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbContext;
    class hkbStateMachine;

    class hkbStateChooser : public hkReferencedObject
    {
    public:
        HK_FORCE_INLINE hkbStateChooser() {}
        HK_FORCE_INLINE hkbStateChooser(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag) {}

        /// @brief The id of the state to start in; -1 falls back to the machine's start state.
        /// @note Name provisional.
        virtual int chooseStateId(const hkbContext& context, hkbStateMachine& sm) = 0;   ///< slot 3
    };
} }
