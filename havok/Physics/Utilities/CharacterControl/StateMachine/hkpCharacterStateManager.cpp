/// @file
/// @brief hkpCharacterStateManager, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterStateManager.h>

namespace CB { namespace havok
{
    hkpCharacterStateManager::hkpCharacterStateManager()
    {
        for (int i = 0; i < HK_CHARACTER_MAX_STATE_ID; i++) m_registeredState[i] = HK_NULL;
    }

    hkpCharacterStateManager::~hkpCharacterStateManager()
    {
        for (int i = 0; i < HK_CHARACTER_MAX_STATE_ID; i++) {
            if (m_registeredState[i]) {
                m_registeredState[i]->removeReference();
                m_registeredState[i] = HK_NULL;
            }
        }
    }

    /// @note The new state is referenced before the old one is released (registering a state over itself is safe).
    void hkpCharacterStateManager::registerState(hkpCharacterState* state, hkpCharacterStateType stateType)
    {
        state->addReference();
        hkpCharacterState* oldState = m_registeredState[stateType];
        if (oldState) oldState->removeReference();
        m_registeredState[stateType] = state;
    }

    hkpCharacterState* hkpCharacterStateManager::getState(hkpCharacterStateType stateType) const
    {
        return m_registeredState[stateType];
    }
} }
