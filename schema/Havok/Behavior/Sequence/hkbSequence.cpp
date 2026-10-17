/// @file
/// @brief hkbSequence, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Sequence/hkbSequence.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Common/Base/Container/String/hkString.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>

namespace CB { namespace schema
{
    /// @note VARIANT: VS2015 24247 stores hkbNode::m_cloneState before loading the vtable address, vanilla after
    ///       (schedule only).
    hkbSequence::hkbSequence()
    :   m_enableEventId(-1),
        m_disableEventId(-1),
        m_time(0.0f),
        m_isEnabled(false)
    {
    }

    /// @note Each loop's bound is read once (vanilla counts in 64 bits against it); each pointer is held across the
    ///       append and gains its reference after.
    hkbSequence::hkbSequence(const hkbSequence& seq)
    :   hkbModifier(seq),
        m_enableEventId(seq.m_enableEventId),
        m_disableEventId(seq.m_disableEventId),
        m_stringData(seq.m_stringData),
        m_variableIdMap(seq.m_variableIdMap),
        m_eventIdMap(seq.m_eventIdMap),
        m_time(0.0f),
        m_isEnabled(false)
    {
        const int numEvents = seq.m_eventSequencedData.getSize();
        for (int i = 0; i < numEvents; i++) {
            hkbEventSequencedData* data = seq.m_eventSequencedData[i];
            m_eventSequencedData.pushBack(data);
            data->addReference();
        }
        const int numReals = seq.m_realVariableSequencedData.getSize();
        for (int i = 0; i < numReals; i++) {
            hkbRealVariableSequencedData* data = seq.m_realVariableSequencedData[i];
            m_realVariableSequencedData.pushBack(data);
            data->addReference();
        }
        const int numBools = seq.m_boolVariableSequencedData.getSize();
        for (int i = 0; i < numBools; i++) {
            hkbBoolVariableSequencedData* data = seq.m_boolVariableSequencedData[i];
            m_boolVariableSequencedData.pushBack(data);
            data->addReference();
        }
        const int numInts = seq.m_intVariableSequencedData.getSize();
        for (int i = 0; i < numInts; i++) {
            hkbIntVariableSequencedData* data = seq.m_intVariableSequencedData[i];
            m_intVariableSequencedData.pushBack(data);
            data->addReference();
        }
    }

    hkbSequence::~hkbSequence()
    {
        for (int i = 0; i < m_eventSequencedData.getSize(); i++) m_eventSequencedData[i]->removeReference();
        for (int i = 0; i < m_realVariableSequencedData.getSize(); i++) m_realVariableSequencedData[i]->removeReference();
        for (int i = 0; i < m_boolVariableSequencedData.getSize(); i++) m_boolVariableSequencedData[i]->removeReference();
        for (int i = 0; i < m_intVariableSequencedData.getSize(); i++) m_intVariableSequencedData[i]->removeReference();
    }

    void hkbSequence::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbSequenceClass, collector);
        collector->addReferencedObject("VariableIdMap", m_variableIdMap);
        collector->addReferencedObject("EventIdMap", m_eventIdMap);
        hkbModifier::calcContentStatistics(collector, &hkbModifierClass);
    }

    /// @note Each next-sample array is sized exactly to its timelines, then zeroed whole. VARIANT: VS2015 24247 loads the
    ///       first timeline count above the m_time store, vanilla below it (schedule only).
    void hkbSequence::activate(const hkbContext& context)
    {
        m_time      = 0.0f;
        m_isEnabled = (m_enableEventId == -1);

        const int numEvents = m_eventSequencedData.getSize();
        m_nextSampleEvents.reserveExactly(numEvents);
        m_nextSampleEvents.setSize(numEvents);
        hkString::memSet4(m_nextSampleEvents.begin(), 0, numEvents);

        const int numReals = m_realVariableSequencedData.getSize();
        m_nextSampleReals.reserveExactly(numReals);
        m_nextSampleReals.setSize(numReals);
        hkString::memSet4(m_nextSampleReals.begin(), 0, numReals);

        const int numBools = m_boolVariableSequencedData.getSize();
        m_nextSampleBools.reserveExactly(numBools);
        m_nextSampleBools.setSize(numBools);
        hkString::memSet4(m_nextSampleBools.begin(), 0, numBools);

        const int numInts = m_intVariableSequencedData.getSize();
        m_nextSampleInts.reserveExactly(numInts);
        m_nextSampleInts.setSize(numInts);
        hkString::memSet4(m_nextSampleInts.begin(), 0, numInts);
    }

    /// @note While enabled, every timeline plays up to the new time; the sequence's own event map is swapped into the
    ///       queue for the duration (so its events translate through it) and the queue's restored after.
    void hkbSequence::update(const hkbContext& context, hkReal timestep)
    {
        if (m_isEnabled) {
            hkbEventQueue* queue = context.getEventQueue();
            m_time += timestep;

            hkbSymbolIdMap* oldMap    = HK_NULL;
            bool            oldDirect = false;
            if (m_eventIdMap) {
                oldDirect            = queue->m_directMap;
                oldMap               = queue->m_symbolIdMap;
                queue->m_symbolIdMap = m_eventIdMap;
                queue->m_directMap   = true;
            }

            for (int i = 0; i < m_eventSequencedData.getSize(); i++)
                m_eventSequencedData[i]->apply(this, context, m_time, queue, m_variableIdMap, m_nextSampleEvents[i]);
            for (int i = 0; i < m_realVariableSequencedData.getSize(); i++)
                m_realVariableSequencedData[i]->apply(this, context, m_time, queue, m_variableIdMap, m_nextSampleReals[i]);
            for (int i = 0; i < m_boolVariableSequencedData.getSize(); i++)
                m_boolVariableSequencedData[i]->apply(this, context, m_time, queue, m_variableIdMap, m_nextSampleBools[i]);
            for (int i = 0; i < m_intVariableSequencedData.getSize(); i++)
                m_intVariableSequencedData[i]->apply(this, context, m_time, queue, m_variableIdMap, m_nextSampleInts[i]);

            context.m_character->m_behaviorGraph->copyVariablesToMembers(context, false);

            if (m_eventIdMap) {
                queue->m_symbolIdMap = oldMap;
                queue->m_directMap   = oldDirect;
            }
        }
    }

    /// @note Written as two guarded tests: vanilla reads m_isEnabled once and tests it on each path.
    void hkbSequence::handleEvent(const hkbContext& context, const hkbEvent e)
    {
        int id = e.getId();
        if (m_eventIdMap) id = m_eventIdMap->getInternal(id);

        if (!m_isEnabled && m_enableEventId == id) {
            m_isEnabled = true;
            m_time      = 0.0f;
        } else if (m_isEnabled && m_disableEventId == id) {
            m_isEnabled = false;
        }
    }

    hkbNode* hkbSequence::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbSequence(*this);
    }

    hkReferencedObject* hkbSequence::createInternalState()
    {
        return new hkbSequenceInternalState;
    }

    void hkbSequence::getInternalState(hkReferencedObject& internalState) const
    {
        hkbSequenceInternalState& state = static_cast<hkbSequenceInternalState&>(internalState);
        state.m_nextSampleEvents        = m_nextSampleEvents;
        state.m_nextSampleReals         = m_nextSampleReals;
        state.m_nextSampleBools         = m_nextSampleBools;
        state.m_nextSampleInts          = m_nextSampleInts;
        state.m_time                    = m_time;
        state.m_isEnabled               = m_isEnabled;
    }

    void hkbSequence::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbSequenceInternalState& state = static_cast<const hkbSequenceInternalState&>(internalState);
        m_nextSampleEvents                    = state.m_nextSampleEvents;
        m_nextSampleReals                     = state.m_nextSampleReals;
        m_nextSampleBools                     = state.m_nextSampleBools;
        m_nextSampleInts                      = state.m_nextSampleInts;
        m_time                                = state.m_time;
        m_isEnabled                           = state.m_isEnabled;
    }
} }
