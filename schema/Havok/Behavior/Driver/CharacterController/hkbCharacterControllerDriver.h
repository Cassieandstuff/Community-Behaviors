/// @file
/// @brief hkbCharacterControllerDriver: only the slots the reimplemented code calls.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbContext;
    struct hkbGeneratorOutput;

    /// @note Slots 3 and 9 are placeholders. No class in the exe implements it (no RTTI), so the slot names are
    ///       provisional, read off the calls of hkbCharacterControllerSceneModifier and hkbCharacterControllerModifier.
    class hkbCharacterControllerDriver : public hkReferencedObject
    {
    public:
        virtual void vfn3();
        /// @brief Moves the controller toward the generated motion, before the physics step.
        virtual void prePhysicsStep(hkReal timestep, const hkbContext& context, hkbGeneratorOutput& generatorOutput);    ///< slot 4
        /// @brief Takes the controller's simulated position back into the character, after the physics step.
        virtual void postPhysicsStep(hkReal timestep, const hkbContext& context, hkbGeneratorOutput& generatorOutput);   ///< slot 5
        /// @brief Whether this controller needs the physics world stepped this frame.
        virtual bool needsPhysicsStep();   ///< slot 6
        /// @brief The controller's current linear velocity (world space).
        /// @note Name provisional, read off hkbCharacterControllerModifier's calls.
        virtual const hkVector4& getLinearVelocity();   ///< slot 7
        /// @brief Whether the controller stands on ground.
        /// @note Name provisional: hkbCharacterControllerModifier stores it into its m_isTouchingGround.
        virtual bool isTouchingGround();   ///< slot 8
        virtual void vfn9();
        /// @note Name provisional: hkbCharacter::deactivate calls it beside the other drivers' resets.
        virtual void reset();   ///< slot 10
    };
} }
