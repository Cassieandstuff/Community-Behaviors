/// @file
/// @brief hkbSequencedData and its four kinds: the timelines an hkbSequence plays (events, real / bool / int variables).
/// @note The folder follows the Havok SDK's layout as remembered (Behavior/Sequence); the exe keeps no __FILE__ string
///       for these classes.
#pragma once

#include <Behavior/Event/hkbEventQueue.h>

namespace CB { namespace schema
{
    class hkbSequence;
    struct hkbContext;

    /// @note A `class`, as in Havok (RTTI `.?AVhkbSequencedData@@`).
    class hkbSequencedData : public hkReferencedObject
    {
    public:
        /// @brief Plays the samples whose time is reached by `time`, starting at `nextSampleIndex` and advancing it: the
        ///        event timeline raises its events into `eventQueue` (sent by `sequence`), the variable timelines set
        ///        their variable (through `variableIdMap` when the sequence has one).
        /// @note Name provisional. hkbSequence::update calls it once per timeline, in reflected-member order.
        virtual void apply(hkbSequence* sequence, const hkbContext& context, hkReal time, hkbEventQueue* eventQueue,
                           hkbSymbolIdMap* variableIdMap, int& nextSampleIndex) = 0;   ///< slot 3
    };

    class hkbEventSequencedData : public hkbSequencedData
    {
    public:
        /// @note 0x20 bytes.
        struct SequencedEvent
        {
            hkbEvent m_event;   ///< +0x00
            hkReal   m_time;    ///< +0x18
        };

        hkArray<SequencedEvent> m_events;   ///< +0x10
    };

    class hkbRealVariableSequencedData : public hkbSequencedData
    {
    public:
        struct Sample
        {
            hkReal m_time;
            hkReal m_value;
        };

        hkArray<Sample> m_samples;         ///< +0x10
        hkInt32         m_variableIndex;   ///< +0x20
    };

    class hkbBoolVariableSequencedData : public hkbSequencedData
    {
    public:
        struct Sample
        {
            hkReal m_time;
            hkBool m_value;
        };

        hkArray<Sample> m_samples;         ///< +0x10
        hkInt32         m_variableIndex;   ///< +0x20
    };

    class hkbIntVariableSequencedData : public hkbSequencedData
    {
    public:
        struct Sample
        {
            hkReal  m_time;
            hkInt32 m_value;
        };

        hkArray<Sample> m_samples;         ///< +0x10
        hkInt32         m_variableIndex;   ///< +0x20
    };
} }
