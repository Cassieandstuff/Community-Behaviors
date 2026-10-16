/// @file
/// @brief hkbEventsFromRangeModifier: raises the event of the range its input value falls in.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @note 0x20 bytes. A range spans from the previous range's upper bound (the modifier's lower bound for the first).
    struct hkbEventRangeData
    {
        enum EventRangeMode
        {
            EVENT_MODE_SEND_ON_ENTER_RANGE = 0,
            EVENT_MODE_SEND_WHEN_IN_RANGE  = 1,
        };

        hkReal           m_upperBound;   ///< +0x00
        hkbEventProperty m_event;        ///< +0x08
        hkInt8           m_eventMode;    ///< +0x18 — EventRangeMode
    };

    class hkbEventRangeDataArray : public hkReferencedObject
    {
    public:
        hkArray<hkbEventRangeData> m_eventData;   ///< +0x10, by increasing upper bound

        HK_FORCE_INLINE hkbEventRangeDataArray() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here (hkbEventRangeDataArray.cpp),
        ///       it also brings the class's vtable and scalar deleting destructor into that unit.
        hkbEventRangeDataArray(hkFinishLoadedObjectFlag flag);
    };

    class hkbEventsFromRangeModifierInternalState : public hkReferencedObject
    {
    public:
        hkArray<hkBool> m_wasActiveInPreviousFrame;   ///< +0x10
    };

    /// @note Sized 0x70. Member names are the reflected ones; the `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: vanilla's (0x140ac6870) is a COMDAT copy the deleting destructor calls.
    class hkbEventsFromRangeModifier : public hkbModifier
    {
    public:
        hkReal                           m_inputValue;                 ///< +0x50
        hkReal                           m_lowerBound;                 ///< +0x54
        hkRefPtr<hkbEventRangeDataArray> m_eventRanges;                ///< +0x58
        hkArray<hkBool>                  m_wasActiveInPreviousFrame;   ///< +0x60, one per range

        hkbEventsFromRangeModifier();
        /// @brief The cloneNode copy: shares the ranges, with no range state.
        /// @note Leaves m_inputValue and m_lowerBound unset, as vanilla does.
        hkbEventsFromRangeModifier(const hkbEventsFromRangeModifier& mod);

        virtual void                activate(const hkbContext& context);                          ///< slot 4
        virtual void                update(const hkbContext& context, hkReal timestep);           ///< slot 5
        virtual bool                isValid(hkStringPtr& errorString) const;                      ///< slot 10
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12
        virtual hkReferencedObject* createInternalState();                                        ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;   ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);   ///< slot 16
    };
} }
