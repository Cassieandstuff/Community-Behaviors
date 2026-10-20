/// @file
/// @brief BSEventEveryNEventsModifier: Bethesda's modifier that raises an event every Nth time it sees another one.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note Member names are the class's reflected names (the last two reflected serialize-ignored). The `slot N`
    ///       annotations are vanilla's vtable slots. The destructor is implicit: the scalar deleting destructor (slot 0)
    ///       inlines it.
    class BSEventEveryNEventsModifier : public hkbModifier
    {
    public:
        hkbEventProperty m_eventToCheckFor;                      ///< +0x50
        hkbEventProperty m_eventToSend;                          ///< +0x60
        hkInt8           m_numberOfEventsBeforeSend;             ///< +0x70 — N, or the upper bound when randomized
        hkInt8           m_minimumNumberOfEventsBeforeSend;      ///< +0x71 — the lower bound when randomized
        hkBool           m_randomizeNumberOfEvents;              ///< +0x72
        int              m_numberOfEventsSeen;                   ///< +0x74 — counts modulo the calculated N
        hkInt8           m_calculatedNumberOfEventsBeforeSend;   ///< +0x78 — the N in use

        /// @note Every constructor sets bits 0 and 1 of m_userData; what they flag is unknown.
        BSEventEveryNEventsModifier();
        BSEventEveryNEventsModifier(hkFinishLoadedObjectFlag flag);
        BSEventEveryNEventsModifier(const BSEventEveryNEventsModifier& mod);

        /// @brief Draws a new N in [minimum, numberOfEventsBeforeSend] from the behavior graph's generator.
        /// @note Name provisional. Keeps the current N when the character has no behavior graph or generator.
        void calculateNumberOfEventsBeforeSend(const hkbContext& context);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                       ///< slot 1
        virtual void        activate(const hkbContext& context);                        ///< slot 4
        virtual void        handleEvent(const hkbContext& context, const hkbEvent e);   ///< slot 6
        virtual bool        isValid(hkStringPtr& errorString) const;                    ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;            ///< slot 12
    };
} }
