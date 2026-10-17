/// @file
/// @brief bhkCharacterCollisionHandler: the game's hook for two characters' controllers touching.
/// @note Not Havok: declared only as far as the character controllers' code calls it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class bhkCharacterController;

    /// @note Opaque beyond the slot the controllers call.
    class bhkCharacterCollisionHandler
    {
    public:
        virtual ~bhkCharacterCollisionHandler();   ///< slot 0 — unverified
        /// @brief `controller` touched `other`.
        /// @note Name provisional.
        virtual void HandleCharacterCollision(bhkCharacterController* controller, bhkCharacterController* other);   ///< slot 1

        /// @brief The installed handler, null for none.
        /// @note Name provisional; its constructor installs it.
        static bhkCharacterCollisionHandler* s_singleton;
    };
} }
