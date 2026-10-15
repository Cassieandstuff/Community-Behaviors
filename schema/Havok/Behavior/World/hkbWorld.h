/// @file
/// @brief hkbWorld and hkbWorldListener: only what the reimplemented code touches.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkbBehaviorGraph;
    class hkbRaycastInterface;
    class hkpWorld;
    struct hkbCharacter;

    /// @brief Told about character changes by the behavior world.
    /// @note Only slot 3 is identified; slots 1–2 are placeholders.
    class hkbWorldListener
    {
    public:
        virtual ~hkbWorldListener();
        virtual void vfn1();
        virtual void vfn2();
        /// @brief `character` switched behavior graphs; `oldBehavior` is the one it had.
        /// @note Name provisional.
        virtual void characterBehaviorChangedCallback(hkbCharacter* character, hkbBehaviorGraph* oldBehavior);   ///< slot 3
    };

    /// @note hkbWorld::step* is dead in Skyrim (BShkbAnimationGraph drives the graphs); the world still carries the
    ///       listeners and the physics/raycast/attachment handles the characters reach through it.
    struct hkbWorld : hkReferencedObject
    {
        char                     m_pad10[0x30 - 0x10];
        hkArray<hkbWorldListener*> m_listeners;            ///< +0x30
        char                     m_pad40[0x130 - 0x40];
        /// @note Name provisional: handed to hkbContext as its attachment manager.
        void*                    m_attachmentManager;      ///< +0x130
        hkpWorld*                m_physicsWorld;           ///< +0x138
        char                     m_pad140[0x148 - 0x140];
        hkbRaycastInterface*     m_raycastInterface;       ///< +0x148

        /// @note Out of line in vanilla (0x140abf4c0).
        const hkArray<hkbWorldListener*>& getListeners();
    };
} }
