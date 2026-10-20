/// @file
/// @brief hkbSequence: a modifier that plays timelines of events and variable values while enabled.
/// @note The folder follows the Havok SDK's layout as remembered (Behavior/Sequence); the exe keeps no __FILE__ string
///       for this class.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Sequence/hkbSequencedData.h>

namespace CB { namespace havok
{
    extern const hkClass hkbSequenceClass;

    /// @note 0x30 bytes; member names after the Havok SDK (unreflected here), the types per the destructor (0x140ac7090).
    class hkbSequenceStringData : public hkReferencedObject
    {
    public:
        hkArray<hkStringPtr> m_eventNames;      ///< +0x10
        hkArray<hkStringPtr> m_variableNames;   ///< +0x20

        HK_FORCE_INLINE hkbSequenceStringData() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here (hkbSequenceStringData.cpp),
        ///       it also brings the class's vtable and scalar deleting destructor into that unit.
        hkbSequenceStringData(hkFinishLoadedObjectFlag flag);
    };

    struct hkbSequenceInternalState : hkReferencedObject
    {
        hkArray<int> m_nextSampleEvents;   ///< +0x10
        hkArray<int> m_nextSampleReals;    ///< +0x20
        hkArray<int> m_nextSampleBools;    ///< +0x30
        hkArray<int> m_nextSampleInts;     ///< +0x40
        hkReal       m_time;               ///< +0x50
        hkBool       m_isEnabled;          ///< +0x54
    };

    /// @note The `slot N` annotations are vanilla's vtable slots (hkbModifier's 25).
    class hkbSequence : public hkbModifier
    {
    public:
        hkArray<hkbEventSequencedData*>        m_eventSequencedData;          ///< +0x50
        hkArray<hkbRealVariableSequencedData*> m_realVariableSequencedData;   ///< +0x60
        hkArray<hkbBoolVariableSequencedData*> m_boolVariableSequencedData;   ///< +0x70
        hkArray<hkbIntVariableSequencedData*>  m_intVariableSequencedData;    ///< +0x80
        hkInt32                                m_enableEventId;               ///< +0x90, -1 = enabled on activate
        hkInt32                                m_disableEventId;              ///< +0x94
        hkRefPtr<hkbSequenceStringData>        m_stringData;                  ///< +0x98
        hkRefPtr<hkbSymbolIdMap>               m_variableIdMap;               ///< +0xa0
        hkRefPtr<hkbSymbolIdMap>               m_eventIdMap;                  ///< +0xa8
        hkArray<int>                           m_nextSampleEvents;            ///< +0xb0, per timeline: its next sample
        hkArray<int>                           m_nextSampleReals;             ///< +0xc0
        hkArray<int>                           m_nextSampleBools;             ///< +0xd0
        hkArray<int>                           m_nextSampleInts;              ///< +0xe0
        hkReal                                 m_time;                        ///< +0xf0
        hkBool                                 m_isEnabled;                   ///< +0xf4

        hkbSequence();
        /// @brief The cloneNode copy: shares the timelines (each gains a reference) and restarts the playback state.
        hkbSequence(const hkbSequence& seq);
        /// @note The timelines are released in the body: the arrays hold raw pointers.
        virtual ~hkbSequence();   ///< slot 0

        virtual void                calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        virtual void                activate(const hkbContext& context);                                    ///< slot 4
        virtual void                update(const hkbContext& context, hkReal timestep);                     ///< slot 5
        virtual void                handleEvent(const hkbContext& context, const hkbEvent e);               ///< slot 6
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                        ///< slot 12
        virtual hkReferencedObject* createInternalState();                                                  ///< slot 13
        /// @pre `internalState` is an hkbSequenceInternalState.
        virtual void                getInternalState(hkReferencedObject& internalState) const;              ///< slot 14
        /// @pre `internalState` is an hkbSequenceInternalState.
        virtual void                setInternalState(const hkReferencedObject& internalState);              ///< slot 16
    };
} }
