/// @file
/// @brief hkbContext: what every node callback is handed — the character, the root behavior and the event queue.
#pragma once

#include <Behavior/Character/hkbCharacter.h>

namespace CB { namespace schema
{
    class hkpWorld;

    struct hkbContext
    {
        hkbCharacter*     m_character;        ///< +0x00
        hkbBehaviorGraph* m_behavior;         ///< +0x08
        void*             m_nodeToIndexMap;   ///< +0x10
        hkbEventQueue*    m_eventQueue;       ///< +0x18
        char              m_pad20[0x30 - 0x20];
        /// @note Name provisional: bits callbacks raise through the const context (hence mutable).
        mutable hkUint8   m_flags;            ///< +0x30
        char              m_pad31[0x48 - 0x31];
        void*             m_animationCache;   ///< +0x48

        /// @brief The physics world: the context's, else the one the character's world holds.
        /// @note Out of line in vanilla.
        hkpWorld* getWorld() const;
        /// @brief The m_flags bit a node raises when its set of active children changes during handleEvent
        ///        (hkbEventDrivenModifier, hkbStateMachine, BSCyclicBlendTransitionGenerator).
        /// @note Name provisional. A constant vanilla reads from memory (0x141941229 = 2), so it is declared, not defined,
        ///       here; its neighbour 0x141941228 = 1 is another such bit.
        static const hkUint8 FLAG_ACTIVE_NODES_CHANGED;

        /// @brief The context's queue, else the character's.
        HK_FORCE_INLINE hkbEventQueue* getEventQueue() const { return m_eventQueue ? m_eventQueue : m_character->m_eventQueue; }
    };
} }
