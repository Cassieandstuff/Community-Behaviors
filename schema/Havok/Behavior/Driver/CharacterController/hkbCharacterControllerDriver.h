/// @file
/// @brief hkbCharacterControllerDriver: only the slots the reimplemented code calls.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbContext;
    struct hkbGeneratorOutput;

    /// @note Slots 3 and 7–9 are placeholders. No class in the exe implements it (no RTTI), so the slot names are
    ///       provisional, read off hkbCharacterControllerSceneModifier's calls.
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
        virtual void vfn7();
        virtual void vfn8();
        virtual void vfn9();
        /// @note Name provisional: hkbCharacter::deactivate calls it beside the other drivers' resets.
        virtual void reset();   ///< slot 10
    };
} }
