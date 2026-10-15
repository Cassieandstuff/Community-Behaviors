/// @file
/// @brief hkbContext: what every node callback is handed — the character, the root behavior and the event queue.
#pragma once

#include <Behavior/Character/hkbCharacter.h>

namespace CB { namespace schema
{
    struct hkbContext
    {
        hkbCharacter*     m_character;        ///< +0x00
        hkbBehaviorGraph* m_behavior;         ///< +0x08
        void*             m_nodeToIndexMap;   ///< +0x10
        hkbEventQueue*    m_eventQueue;       ///< +0x18
        char              m_pad20[0x48 - 0x20];
        void*             m_animationCache;   ///< +0x48

        /// @brief The context's queue, else the character's.
        HK_FORCE_INLINE hkbEventQueue* getEventQueue() const { return m_eventQueue ? m_eventQueue : m_character->m_eventQueue; }
    };
} }
