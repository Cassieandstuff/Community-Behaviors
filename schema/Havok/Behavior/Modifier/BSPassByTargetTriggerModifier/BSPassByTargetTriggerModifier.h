/// @file
/// @brief BSPassByTargetTriggerModifier: raises an event when its character passes by a target point.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @brief The game's hook into the modifier: whether it may fire, and where its target and movement come from.
    /// @note The class name is the exe's RTTI; the game installs BSPassByTargetTriggerModifierInterface. Virtual names
    ///       provisional, after what the game's overrides do.
    class BSIPassByTargetTriggerModifierSingleton
    {
    public:
        virtual ~BSIPassByTargetTriggerModifierSingleton();                                   ///< slot 0
        /// @brief The game's: false while the player rides the character.
        virtual bool canTrigger(const hkbContext& context);                                   ///< slot 1
        /// @brief The game's: the camera's position (w = 0).
        virtual void getTargetPosition(hkVector4& positionOut);                               ///< slot 2
        /// @brief The game's: the character's actor's movement (w = 0); untouched for a non-actor.
        virtual void getMovementDirection(const hkbContext& context, hkVector4& directionOut);   ///< slot 3

        /// @brief The installed singleton; null until the game sets it at startup.
        /// @note Name provisional.
        static BSIPassByTargetTriggerModifierSingleton* ms_pSingleton;
    };

    /// @note Member names are the class's reflected names; m_targetPassed is runtime only. The destructor is implicit:
    ///       vanilla's deleting destructor (slot 0) inlines it.
    class BSPassByTargetTriggerModifier : public hkbModifier
    {
    public:
        hkVector4        m_targetPosition;      ///< +0x50
        hkReal           m_radius;              ///< +0x60
        hkVector4        m_movementDirection;   ///< +0x70
        hkbEventProperty m_triggerEvent;        ///< +0x80
        /// @note Name provisional (the reflection skips it as padding).
        hkBool           m_targetPassed;        ///< +0x90

        BSPassByTargetTriggerModifier();
        /// @note Called by the class's packfile finish function (in the type-registration block).
        BSPassByTargetTriggerModifier(hkFinishLoadedObjectFlag flag);
        BSPassByTargetTriggerModifier(const BSPassByTargetTriggerModifier& mod);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                         ///< slot 1
        virtual void        activate(const hkbContext& context);                          ///< slot 4
        virtual bool        isValid(hkStringPtr& errorString) const;                      ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12
        /// @brief Within m_radius of the target, raises m_triggerEvent on the frame the movement turns away from it.
        virtual void        modify(const hkbContext& context, hkbGeneratorOutput& inOut); ///< slot 23
    };
    static_assert(sizeof(BSPassByTargetTriggerModifier) == 0xa0, "BSPassByTargetTriggerModifier size");
} }
