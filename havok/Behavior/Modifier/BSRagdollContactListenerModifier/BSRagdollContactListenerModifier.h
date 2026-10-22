/// @file
/// @brief BSRagdollContactListenerModifier: Bethesda's modifier that raises an event when listed ragdoll bones touch
///        something else.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbBoneIndexArray.h>
#include <Physics/Dynamics/Collide/ContactListener/hkpContactListener.h>

namespace CB { namespace havok
{
    class hkpRigidBody;
    class BSRagdollContactListenerModifier;

    /// @brief The game's hook for switching a modifier's listeners on and off (Skyrim defers it to its physics thread).
    /// @note RTTI name BSIRagdollContactListenerModifierSingleton; vtable 0x1419834a0 (slot 0 the scalar deleting
    ///       destructor, slot 1 pure). The slot-1 name is provisional.
    class BSIRagdollContactListenerModifierSingleton
    {
    public:
        /// @note Both out of line in vanilla (0x140b95270, 0x140b95290), in this unit.
        BSIRagdollContactListenerModifierSingleton();
        virtual ~BSIRagdollContactListenerModifierSingleton();   ///< slot 0
        /// @brief Adds (`activate`) or removes `modifier`'s contact listeners.
        virtual void SetListenersActive(const hkbContext& context, BSRagdollContactListenerModifier* modifier, bool activate) = 0;   ///< slot 1

        /// @brief The installed handler; null until the game sets one.
        /// @note Name provisional (0x1431bdfd0).
        static BSIRagdollContactListenerModifierSingleton* s_instance;
    };

    /// @brief Listens to the ragdoll bodies of m_bones while active; a contact with anything else (penetrating) makes the
    ///        next update raise m_contactEvent.
    /// @note 0x88 bytes. Member names are the class's reflected names. The contact listener sits at +0x50. Every
    ///       constructor sets bit 1 of m_userData.
    class BSRagdollContactListenerModifier : public hkbModifier, public hkpContactListener
    {
    public:
        hkbEventProperty            m_contactEvent;         ///< +0x58
        hkRefPtr<hkbBoneIndexArray> m_bones;                ///< +0x68
        /// @brief Raised by the contact callback, consumed (cleared) by update.
        hkBool                      m_throwEvent;           ///< +0x70
        /// @brief The bodies this modifier listens to (each holds a reference).
        hkArray<hkpRigidBody*>      m_ragdollRigidBodies;   ///< +0x78

        BSRagdollContactListenerModifier();
        BSRagdollContactListenerModifier(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: shares m_bones, or copies it when its bones are bound to variables.
        BSRagdollContactListenerModifier(const BSRagdollContactListenerModifier& mod);
        /// @note Out of line in vanilla (0x140b95420); the scalar deleting destructor calls it.
        virtual ~BSRagdollContactListenerModifier();

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                  ///< slot 1
        virtual void        collectBindables(hkbBindableCollector& collector);     ///< slot 3
        virtual void        activate(const hkbContext& context);                   ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);    ///< slot 5
        virtual void        deactivate(const hkbContext& context);                 ///< slot 7
        /// @brief Needs an event, and no unset or repeated bone index.
        virtual bool        isValid(hkStringPtr& errorString) const;              ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;       ///< slot 12

        /// @brief Flags m_throwEvent for a penetrating contact unless both bodies are listened ones.
        virtual void contactPointCallback(const hkpContactPointEvent& event);   ///< hkpContactListener slot 0

        /// @brief Through the game's handler when one is installed, else directly: addListeners or removeListeners.
        /// @note Name provisional; out of line in vanilla (0x140b95230). activate and deactivate tail-jump to it.
        static void setListenersActive(const hkbContext& context, BSRagdollContactListenerModifier* modifier, bool activate);
        /// @brief Drops the old listeners, then listens to the ragdoll body of each of m_bones (switching keyframed
        ///        bodies to keyframed-reporting quality so they report contacts).
        /// @note Name provisional; out of line in vanilla (0x140b95ae0).
        void addListeners(const hkbContext& context);
        /// @brief Stops listening to (and releases) every body in m_ragdollRigidBodies.
        /// @note Name provisional; out of line in vanilla (0x140b95ca0).
        void removeListeners();
    };
} }
