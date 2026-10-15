/// @file
/// @brief hkbContext: what every node callback is handed — the character, the root behavior and the event queue.
#pragma once

#include <Behavior/Character/hkbCharacter.h>

namespace CB { namespace schema
{
    class hkpWorld;
    class hkbNode;

    struct hkbContext
    {
        hkbCharacter*     m_character;        ///< +0x00
        hkbBehaviorGraph* m_behavior;         ///< +0x08
        void*             m_nodeToIndexMap;   ///< +0x10
        hkbEventQueue*    m_eventQueue;       ///< +0x18
        char              m_pad20[0x30 - 0x20];
        /// @note Name provisional: bits callbacks raise through the const context (hence mutable).
        mutable hkUint8   m_flags;            ///< +0x30
        char              m_pad31[0x38 - 0x31];
        hkpWorld*         m_world;            ///< +0x38
        char              m_pad40[0x48 - 0x40];
        void*             m_animationCache;   ///< +0x48

        /// @brief The m_flags bit a node raises when its set of active children changes during handleEvent
        ///        (hkbEventDrivenModifier, hkbStateMachine, BSCyclicBlendTransitionGenerator).
        /// @note Name provisional. A constant vanilla reads from memory (0x141941229 = 2), so it is declared, not defined,
        ///       here; its neighbour 0x141941228 = 1 is another such bit.
        static const hkUint8 FLAG_ACTIVE_NODES_CHANGED;

        /// @brief A context for `character` with no behavior, queue or flags yet.
        /// @note Out of line in vanilla (0x140ab0a60). Parameter names provisional: hkbCharacter passes its behavior
        ///       world's +0x138 / +0x130 and a null cache.
        hkbContext(hkbCharacter* character, hkpWorld* world, void* attachmentManager, void* animationCache);

        /// @brief The context's queue, else the character's.
        HK_FORCE_INLINE hkbEventQueue* getEventQueue() const { return m_eventQueue ? m_eventQueue : m_character->m_eventQueue; }
        /// @brief The context's physics world, else the character's behavior world's (null without either).
        /// @note Name provisional. Out of line in vanilla (0x140ab0aa0).
        hkpWorld* getWorld() const;
        /// @brief The behavior world's up vector, else the character's project data's.
        /// @note Out of line in vanilla (0x140ab0ad0).
        const hkVector4& getWorldUpWS() const;
        /// @brief Whether `node` is active: in the context's node-to-index map when it has one, else in the root graph's
        ///        active nodes (the root graph itself counts while the graph is active).
        /// @note Name provisional. Out of line in vanilla (0x140ab0b40).
        bool isNodeActive(const hkbNode* node) const;
    };
} }
