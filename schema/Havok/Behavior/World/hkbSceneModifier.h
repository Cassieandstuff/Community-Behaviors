/// @file
/// @brief hkbSceneModifier: a pass the behavior world runs over all its characters after they generate.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbCharacter;
    struct hkbContext;
    struct hkbGeneratorOutput;

    /// @brief The characters a scene pass sees.
    /// @note Names provisional.
    struct hkbSceneCharacters
    {
        /// @brief What a scene pass hands a character's drivers: the character's output this frame and its context.
        struct CharacterInfo
        {
            hkbGeneratorOutput* m_generatorOutput;   ///< +0x00
            const hkbContext*   m_context;           ///< +0x08
        };

        hkPointerMap<hkbCharacter*, CharacterInfo*> m_characters;            ///< +0x00
        /// @brief Raised by the character controller pass once it has stepped the physics world, so the ragdoll pass
        ///        does not step it a second time.
        hkBool                                      m_physicsWorldStepped;   ///< +0x10
    };

    class hkpWorld;

    /// @brief Steps a physics world in place of hkpWorld::stepDeltaTime, for the scene passes that step physics
    ///        (ragdoll, character controller).
    /// @note Name and slot name provisional: an interface with no RTTI in the exe, called through slot 0.
    class hkbPhysicsWorldStepper
    {
    public:
        virtual void stepPhysicsWorld(hkpWorld* world, hkReal timestep) = 0;   ///< slot 0
    };

    /// @note hkbWorld's constructor builds its scene modifiers (attachment, ragdoll, character controller, foot/hand IK and
    ///       their fixups) and hands each its world member; the generate passes call modify on each in turn.
    class hkbSceneModifier : public hkReferencedObject
    {
    public:
        /// @brief Runs the pass over `sceneCharacters`.
        /// @note Name provisional (the Havok SDK's); slot 3, after hkReferencedObject's.
        virtual void modify(hkbSceneCharacters& sceneCharacters, hkReal timestep) = 0;   ///< slot 3
    };
} }
