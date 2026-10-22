/// @file
/// @brief hkbEventDrivenModifier: runs its child modifier between an activate and a deactivate event.
#pragma once

#include <Behavior/Modifier/hkbModifierWrapper.h>

namespace CB { namespace havok
{
    struct hkbEventDrivenModifierInternalState : hkReferencedObject
    {
        hkBool m_isActive;   ///< +0x10
    };

    /// @note The `slot N` annotations are vanilla's vtable slots (isValid stays hkbModifierWrapper's). The destructor is
    ///       implicit.
    class hkbEventDrivenModifier : public hkbModifierWrapper
    {
    public:
        hkInt32 m_activateEventId;     ///< +0x58, -1 = none
        hkInt32 m_deactivateEventId;   ///< +0x5c, -1 = none
        hkBool  m_activeByDefault;     ///< +0x60
        hkBool  m_isActive;            ///< +0x61

        hkbEventDrivenModifier();
        /// @brief The cloneNode copy: the settings, not yet active.
        hkbEventDrivenModifier(const hkbEventDrivenModifier& mod);

        /// @note Out of line among the type-registration getters (0x140ac4020).
        static const hkClass& staticClass();

        virtual const void*         getClassType() const;                                             ///< slot 1
        virtual void                activate(const hkbContext& context);                              ///< slot 4
        /// @brief Switches the child on (off) on the activate (deactivate) event, and flags the change to the root graph
        ///        and the context.
        virtual void                handleEvent(const hkbContext& context, const hkbEvent e);         ///< slot 6
        /// @brief The child only while it is running (under FLAG_ACTIVE_ONLY), never under FLAG_GENERATORS_ONLY.
        virtual void                getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);  ///< slot 9
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                getActiveEvents(hkPointerMap<int, bool>& activeEvents) const;     ///< slot 18
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);     ///< slot 23
        virtual void                updateSync(const hkbContext& context, hkbNodeInfo& info);         ///< slot 24
    };
} }
