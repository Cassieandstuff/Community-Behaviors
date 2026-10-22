/// @file
/// @brief The sequenced-data timelines' playback (slot 3), reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Sequence/hkbSequencedData.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Sequence/hkbSequence.h>

namespace CB { namespace havok
{
    namespace
    {
        /// @brief `variableIndex` (local to the sequence's graph) as the root graph's variable index; -1 when the root
        ///        graph has no such variable.
        HK_FORCE_INLINE int toRootVariableIndex(const hkbContext& context, hkbSymbolIdMap* variableIdMap, int variableIndex)
        {
            return hkbSymbolIdMap::convertInternalId(variableIdMap, context.m_character->m_behaviorGraph->m_variableIdMap, variableIndex);
        }
    }

    hkbEventSequencedData::hkbEventSequencedData(hkFinishLoadedObjectFlag flag) : hkbSequencedData(flag), m_events(flag)
    {
    }

    hkbRealVariableSequencedData::hkbRealVariableSequencedData(hkFinishLoadedObjectFlag flag) : hkbSequencedData(flag), m_samples(flag)
    {
    }

    hkbBoolVariableSequencedData::hkbBoolVariableSequencedData(hkFinishLoadedObjectFlag flag) : hkbSequencedData(flag), m_samples(flag)
    {
    }

    hkbIntVariableSequencedData::hkbIntVariableSequencedData(hkFinishLoadedObjectFlag flag) : hkbSequencedData(flag), m_samples(flag)
    {
    }

    /// @note Every event due by `time` goes out with no payload, sent by the sequence.
    void hkbEventSequencedData::apply(hkbSequence* sequence, const hkbContext& context, hkReal time, hkbEventQueue* eventQueue,
                                      hkbSymbolIdMap* variableIdMap, int& nextSampleIndex)
    {
        const int numEvents = m_events.getSize();
        if (numEvents != 0) {
            while (nextSampleIndex < numEvents) {
                if (time < m_events[nextSampleIndex].m_time) break;
                hkbEvent e(m_events[nextSampleIndex].m_event.m_id, HK_NULL, sequence);
                e.m_id = eventQueue->toExternal(e.m_id);
                eventQueue->enqueue(e);
                nextSampleIndex++;
            }
        }
    }

    /// @note Interpolates linearly between the samples around `time`; before the first or past the last it holds that one.
    void hkbRealVariableSequencedData::apply(hkbSequence* sequence, const hkbContext& context, hkReal time, hkbEventQueue* eventQueue,
                                             hkbSymbolIdMap* variableIdMap, int& nextSampleIndex)
    {
        const int numSamples = m_samples.getSize();
        if (numSamples != 0) {
            while (nextSampleIndex < numSamples) {
                if (time < m_samples[nextSampleIndex].m_time) break;
                nextSampleIndex++;
            }

            const int i = nextSampleIndex;
            hkReal    value;
            if (i == 0) {
                value = m_samples[0].m_value;
            } else if (i == numSamples) {
                value = m_samples[numSamples - 1].m_value;
            } else {
                const hkReal t = (time - m_samples[i - 1].m_time) / (m_samples[i].m_time - m_samples[i - 1].m_time);
                value = (1.0f - t) * m_samples[i - 1].m_value + t * m_samples[i].m_value;
            }

            const int index = toRootVariableIndex(context, variableIdMap, m_variableIndex);
            if (index != -1) {
                union { hkInt32 i; hkReal r; } u;
                u.r = value;
                context.m_character->m_behaviorGraph->m_variableValueSet->m_wordVariableValues[index].m_value = u.i;
            }
        }
    }

    /// @note Steps: the nearer sample wins (the later one from halfway).
    void hkbBoolVariableSequencedData::apply(hkbSequence* sequence, const hkbContext& context, hkReal time, hkbEventQueue* eventQueue,
                                             hkbSymbolIdMap* variableIdMap, int& nextSampleIndex)
    {
        const int numSamples = m_samples.getSize();
        if (numSamples != 0) {
            while (nextSampleIndex < numSamples) {
                if (time < m_samples[nextSampleIndex].m_time) break;
                nextSampleIndex++;
            }

            const int i = nextSampleIndex;
            hkBool    value;
            if (i == 0) {
                value = m_samples[0].m_value;
            } else if (i == numSamples) {
                value = m_samples[numSamples - 1].m_value;
            } else {
                const hkReal t = (time - m_samples[i - 1].m_time) / (m_samples[i].m_time - m_samples[i - 1].m_time);
                if (t < 0.5f) {
                    value = m_samples[i - 1].m_value;
                } else {
                    value = m_samples[i].m_value;
                }
            }

            const int index = toRootVariableIndex(context, variableIdMap, m_variableIndex);
            if (index != -1) {
                context.m_character->m_behaviorGraph->m_variableValueSet->m_wordVariableValues[index].m_value = value ? 1 : 0;
            }
        }
    }

    /// @note Steps: the nearer sample wins (the later one from halfway).
    void hkbIntVariableSequencedData::apply(hkbSequence* sequence, const hkbContext& context, hkReal time, hkbEventQueue* eventQueue,
                                            hkbSymbolIdMap* variableIdMap, int& nextSampleIndex)
    {
        const int numSamples = m_samples.getSize();
        if (numSamples != 0) {
            while (nextSampleIndex < numSamples) {
                if (time < m_samples[nextSampleIndex].m_time) break;
                nextSampleIndex++;
            }

            const int i = nextSampleIndex;
            hkInt32   value;
            if (i == 0) {
                value = m_samples[0].m_value;
            } else if (i == numSamples) {
                value = m_samples[numSamples - 1].m_value;
            } else {
                const hkReal t = (time - m_samples[i - 1].m_time) / (m_samples[i].m_time - m_samples[i - 1].m_time);
                value = (t < 0.5f) ? m_samples[i - 1].m_value : m_samples[i].m_value;
            }

            const int index = toRootVariableIndex(context, variableIdMap, m_variableIndex);
            if (index != -1) {
                context.m_character->m_behaviorGraph->m_variableValueSet->m_wordVariableValues[index].m_value = value;
            }
        }
    }
} }
