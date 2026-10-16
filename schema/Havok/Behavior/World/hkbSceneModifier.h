/// @file
/// @brief hkbSceneModifier: a pass the behavior world runs over all its characters after they generate.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbCharacter;

    /// @brief The characters a scene pass sees.
    /// @note Only what the reimplemented code reads: the character map (16-byte pairs, the value not identified) and a
    ///       flag the ragdoll scene modifier tests at +0x10. Names provisional.
    struct hkbSceneCharacters
    {
        hkPointerMap<hkbCharacter*, void*> m_characters;   ///< +0x00
        hkBool                             m_flag10;       ///< +0x10
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
