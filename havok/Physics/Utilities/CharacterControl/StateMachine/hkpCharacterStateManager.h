/// @file
/// @brief hkpCharacterStateManager: the table of states a character context runs, by state id.
#pragma once

#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterState.h>

namespace CB { namespace havok
{
    /// @note 0x68 bytes (the deleting destructor's fallback size). Not reflected.
    class hkpCharacterStateManager : public hkReferencedObject
    {
    public:
        /// @brief Referenced states; null where none is registered.
        hkpCharacterState* m_registeredState[HK_CHARACTER_MAX_STATE_ID];   ///< +0x10

        hkpCharacterStateManager();
        /// @brief Releases every registered state.
        ~hkpCharacterStateManager();

        /// @brief Registers `state` under `stateType`, releasing the state it replaces.
        void               registerState(hkpCharacterState* state, hkpCharacterStateType stateType);
        hkpCharacterState* getState(hkpCharacterStateType stateType) const;
    };
} }
