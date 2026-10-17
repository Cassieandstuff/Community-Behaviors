/// @file
/// @brief hkbWorld and hkbWorldListener: only what the reimplemented code touches.
#pragma once

#include <Common/Base/hkBase.h>
#include <Common/Base/Thread/CriticalSection/hkCriticalSection.h>

namespace CB { namespace schema
{
    class hkbBehaviorGraph;
    class hkaRaycastInterface;
    class hkbSceneModifier;
    class hkpWorld;
    struct hkbAttachmentManager;
    struct hkbCharacter;
    struct hkbWorld;

    /// @brief Told about character changes by the behavior world.
    /// @note Slots 1 and 4–7 are placeholders: nothing reimplemented calls them yet.
    class hkbWorldListener
    {
    public:
        virtual ~hkbWorldListener();
        virtual void vfn1();
        /// @brief `character` left the world (it still holds its reference).
        /// @note Name provisional.
        virtual void characterRemovedCallback(hkbCharacter* character);   ///< slot 2
        /// @brief `character` switched behavior graphs; `oldBehavior` is the one it had.
        /// @note Name provisional.
        virtual void characterBehaviorChangedCallback(hkbCharacter* character, hkbBehaviorGraph* oldBehavior);   ///< slot 3
        virtual void vfn4();
        virtual void vfn5();
        virtual void vfn6();
        virtual void vfn7();
        /// @brief `world` is being destroyed.
        /// @note Name provisional.
        virtual void worldDeletedCallback(hkbWorld* world);   ///< slot 8
    };

    /// @brief One of the world's four name-to-id linkers (events, variables, attributes, character properties).
    /// @note Name provisional; 0x38 bytes. Its destructor is out of line in vanilla (0x140ac1270); its members are not
    ///       identified yet.
    struct hkbSymbolLinker
    {
        hkUint8 m_opaque[0x38];

        ~hkbSymbolLinker();
    };

    /// @note hkbWorld::step* is dead in Skyrim (BShkbAnimationGraph drives the graphs); the world still carries the
    ///       listeners and the physics/raycast/attachment handles the characters reach through it. 0x160 bytes.
    struct hkbWorld : hkReferencedObject
    {
        /// @brief A heap block the world owns (+0x150): an array of 0x20-byte entries under a lock.
        /// @note Name and contents provisional; 0x38 bytes.
        struct LockedArray
        {
            struct Entry
            {
                hkUint8 m_opaque[0x20];
            };

            hkArray<Entry>    m_entries;   ///< +0x00
            hkCriticalSection m_lock;      ///< +0x10

            HK_FORCE_INLINE void operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }
        };

        hkArray< hkRefPtr<hkbSceneModifier> > m_sceneModifiers;            ///< +0x10
        hkArray<hkbCharacter*>               m_characters;                ///< +0x20 — each referenced
        hkArray<hkbWorldListener*>           m_listeners;                 ///< +0x30
        hkbSymbolLinker                      m_eventLinker;               ///< +0x40 — the four names provisional
        hkbSymbolLinker                      m_variableLinker;            ///< +0x78
        hkbSymbolLinker                      m_attributeLinker;           ///< +0xb0
        hkbSymbolLinker                      m_characterPropertyLinker;   ///< +0xe8
        char                                 m_pad120[0x130 - 0x120];
        /// @note Owned: the destructor deletes it. Handed to hkbContext as its attachment manager.
        hkbAttachmentManager*                m_attachmentManager;         ///< +0x130
        hkpWorld*                            m_physicsWorld;              ///< +0x138
        char                                 m_pad140[0x148 - 0x140];
        /// @note Owned: the destructor deletes it.
        hkaRaycastInterface*                 m_raycastInterface;          ///< +0x148
        /// @note Owned: the destructor deletes it. Name provisional.
        LockedArray*                         m_lockedArray;               ///< +0x150
        char                                 m_pad158[0x160 - 0x158];

        /// @brief Removes every character (deactivating it), tells the listeners, then frees what the world owns.
        ~hkbWorld();

        /// @note Out of line in vanilla (0x140abf4c0).
        const hkArray<hkbWorldListener*>& getListeners();
        /// @brief Takes `character` out of the world (deactivating it first when `deactivate`), tells the listeners
        ///        and drops the world's reference.
        /// @note Name and parameter names provisional.
        void removeCharacter(hkbCharacter* character, bool deactivate);
    };
} }
