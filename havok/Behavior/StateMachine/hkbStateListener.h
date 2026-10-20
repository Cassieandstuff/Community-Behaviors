/// @file
/// @brief hkbStateListener: told when a state machine's state is entered, updated and exited.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    struct hkbContext;
    class hkbStateMachine;

    /// @note Slots 3–6 per vanilla's vtables (AE 1.6.1170). hkbStateListener's own 4 and 5 are empty; the names of 4 and 5
    ///       (and which is which) are the Havok SDK's order, unverified.
    class hkbStateListener : public hkReferencedObject
    {
    public:
        virtual void              onEnter(const hkbContext& context, hkbStateMachine* sm);                   ///< slot 3
        virtual void              onUpdate(const hkbContext& context, hkReal timestep, hkbStateMachine* sm); ///< slot 4
        virtual void              onExit(const hkbContext& context, hkbStateMachine* sm);                    ///< slot 5
        /// @brief The listener a cloned state machine takes.
        virtual hkbStateListener* clone();                                                                   ///< slot 6
    };
} }
