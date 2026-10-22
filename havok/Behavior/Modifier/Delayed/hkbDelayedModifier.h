/// @file
/// @brief hkbDelayedModifier: runs its child modifier only after a delay, optionally for a limited duration.
#pragma once

#include <Behavior/Modifier/hkbModifierWrapper.h>

namespace CB { namespace havok
{
    struct hkbDelayedModifierInternalState : hkReferencedObject
    {
        hkReal m_secondsElapsed;   ///< +0x10
        hkBool m_isActive;         ///< +0x14
    };

    /// @note No reflected descriptor in schema/ yet; member names after the Havok SDK. The `slot N` annotations are
    ///       vanilla's vtable slots (getClassType stays hkReferencedObject's, isValid hkbModifierWrapper's). The
    ///       destructor is implicit.
    class hkbDelayedModifier : public hkbModifierWrapper
    {
    public:
        hkReal m_delaySeconds;      ///< +0x58
        /// @brief 0 = no limit.
        hkReal m_durationSeconds;   ///< +0x5c
        hkReal m_secondsElapsed;    ///< +0x60, since activation
        hkBool m_isActive;          ///< +0x64

        hkbDelayedModifier();
        /// @brief The cloneNode copy: the settings, not yet active.
        hkbDelayedModifier(const hkbDelayedModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        /// @brief Advances the clock; flags the root graph to re-gather its active nodes when that switches the child.
        virtual void                update(const hkbContext& context, hkReal timestep);               ///< slot 5
        /// @brief The child only while it is running (under FLAG_ACTIVE_ONLY), never under FLAG_GENERATORS_ONLY.
        virtual void                getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);  ///< slot 9
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);     ///< slot 23
        virtual void                updateSync(const hkbContext& context, hkbNodeInfo& info);         ///< slot 24

        /// @brief Whether the child runs now: it exists, the delay has passed, and the duration (if any) has not.
        /// @note Name provisional.
        bool isDelayElapsed() const;
    };
} }
