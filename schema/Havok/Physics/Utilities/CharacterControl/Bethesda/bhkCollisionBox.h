/// @file
/// @brief bhkCollisionBox, the box phantom a character controller keeps around its body.
/// @note Declared only as far as the character controller's code (and UpdateFromController) uses it.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCachingShapePhantom.h>

namespace CB { namespace schema
{
    class bhkCharacterController;

    /// @note 0x40 bytes; a bhkSimpleShapePhantom (no members of its own here) in vanilla's hierarchy.
    class bhkCollisionBox : public bhkShapePhantom
    {
    public:
        /// @brief Moves the box onto its controller's body: its node's world transform and bound center when the body
        ///        has a node, else the controller's position.
        /// @note Name provisional; out of line in vanilla (0x140f035b0).
        void UpdateFromController();

        /// @brief Stops every box following its controller.
        /// @note Name provisional; a flag in vanilla's .data.
        static bool s_frozen;

        bhkCharacterController* m_controller;   ///< +0x30
        hkUint8                 m_pad38[0x40 - 0x38];
    };
} }
