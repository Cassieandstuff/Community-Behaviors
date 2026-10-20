/// @file
/// @brief hkbCharacterControllerModifier: drives the physics character controller — follows the animation's motion, or
///        moves dynamically from the controller's own velocity — through the generator output's controller track.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Driver/CharacterController/hkbCharacterControllerControlData.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note 0x30 bytes. Member names are the exe's reflected strings.
    struct hkbCharacterControllerModifierInternalState : hkReferencedObject
    {
        hkVector4 m_gravity;                  ///< +0x10
        hkReal    m_timestep;                 ///< +0x20
        hkBool    m_isInitialVelocityAdded;   ///< +0x24
        hkBool    m_isTouchingGround;         ///< +0x25

        HK_FORCE_INLINE hkbCharacterControllerModifierInternalState() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into hkbCharacterControllerModifier.cpp.
        hkbCharacterControllerModifierInternalState(hkFinishLoadedObjectFlag flag);
    };

    /// @note 0xb0 bytes. Member and enum names are the exe's reflected strings (not in schema/); the layout and
    ///       defaults are vanilla's. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines
    ///       it.
    class hkbCharacterControllerModifier : public hkbModifier
    {
    public:
        enum InitialVelocityCoordinates
        {
            INITIAL_VELOCITY_IN_WORLD_COORDINATES = 0,
            INITIAL_VELOCITY_IN_MODEL_COORDINATES = 1,
        };
        enum MotionMode
        {
            /// @brief The desired velocity is the animation's motion over the accumulated timestep.
            MOTION_MODE_FOLLOW_ANIMATION = 0,
            /// @brief The desired velocity is the controller's own, plus the initial velocity once.
            MOTION_MODE_DYNAMIC          = 1,
        };

        hkbCharacterControllerControlData m_controlData;                   ///< +0x50
        hkVector4                         m_initialVelocity;               ///< +0x70
        hkInt8                            m_initialVelocityCoordinates;    ///< +0x80 — InitialVelocityCoordinates
        hkInt8                            m_motionMode;                    ///< +0x81 — MotionMode
        /// @brief Never let the desired velocity point up more than the controller's current one does (along the world's
        ///        up).
        hkBool                            m_forceDownwardMomentum;         ///< +0x82
        /// @brief Add gravity over the timestep while the controller is airborne.
        hkBool                            m_applyGravity;                  ///< +0x83
        hkBool                            m_setInitialVelocity;            ///< +0x84
        hkBool                            m_isTouchingGround;              ///< +0x85
        /// @brief The physics world's gravity, read at activate.
        hkVector4                         m_gravity;                       ///< +0x90
        /// @brief The time accumulated by update and not yet consumed by modify.
        hkReal                            m_timestep;                      ///< +0xa0
        hkBool                            m_isInitialVelocityAdded;        ///< +0xa4

        hkbCharacterControllerModifier();
        /// @brief The cloneNode copy: the settings, with the runtime state reset.
        /// @note The settings (m_controlData through m_isTouchingGround) are one 0x36-byte memCpy.
        hkbCharacterControllerModifier(const hkbCharacterControllerModifier& mod);

        virtual void                activate(const hkbContext& context);                            ///< slot 4
        virtual void                update(const hkbContext& context, hkReal timestep);             ///< slot 5
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual hkReferencedObject* createInternalState();                                          ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;     ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);     ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
