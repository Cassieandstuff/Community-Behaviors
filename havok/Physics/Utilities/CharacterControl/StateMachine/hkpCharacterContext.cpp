/// @file
/// @brief hkpCharacterContext, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterContext.h>
#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterStateManager.h>

namespace CB { namespace havok
{
    hkpCharacterContext::hkpCharacterContext(const hkpCharacterStateManager* manager, hkpCharacterStateType initialState)
    :   m_stateManager(manager)
    {
        m_currentState          = initialState;
        m_previousState         = initialState;
        m_characterType         = HK_CHARACTER_PROXY;
        m_filterEnable          = true;
        m_maxLinearVelocity     = 20.0f;
        m_maxLinearAcceleration = 625.0f;
        m_gain                  = 1.0f;
        m_stateManager->addReference();
    }

    hkpCharacterContext::~hkpCharacterContext()
    {
        m_stateManager->removeReference();
    }

    hkpCharacterStateType hkpCharacterContext::getState() const
    {
        return m_currentState;
    }

    void hkpCharacterContext::setState(hkpCharacterStateType state, const hkpCharacterInput& input, hkpCharacterOutput& output)
    {
        const hkpCharacterStateType prevState = m_currentState;
        m_stateManager->getState(m_currentState)->leaveState(*this, state, input, output);
        m_currentState = state;
        m_stateManager->getState(m_currentState)->enterState(*this, prevState, input, output);
    }

    void hkpCharacterContext::update(const hkpCharacterInput& input, hkpCharacterOutput& output)
    {
        output.m_velocity = input.m_velocity;

        m_stateManager->getState(m_currentState)->change(*this, input, output);
        if (m_currentState == m_previousState) m_stateManager->getState(m_currentState)->update(*this, input, output);
        m_previousState = m_currentState;

        if (m_filterEnable) {
            const hkReal currentVelocity = output.m_velocity.length3().getReal();
            if (currentVelocity > m_maxLinearVelocity) output.m_velocity.setMul4(hkSimdReal(m_maxLinearVelocity / currentVelocity), output.m_velocity);

            hkVector4 acceleration;
            acceleration.setSub4(output.m_velocity, input.m_velocity);
            acceleration.mul4(hkSimdReal(input.m_stepInfo.m_invDeltaTime));
            const hkReal accelerationRatio = acceleration.length3().getReal() / m_maxLinearAcceleration;

            output.m_velocity = input.m_velocity;
            if ((accelerationRatio > 1.0f) && (m_currentState != HK_CHARACTER_JUMPING))
                output.m_velocity.addMul4(hkSimdReal(input.m_stepInfo.m_deltaTime * m_gain / accelerationRatio), acceleration);
            else
                output.m_velocity.addMul4(hkSimdReal(input.m_stepInfo.m_deltaTime * m_gain), acceleration);
        }
    }

    void hkpCharacterContext::setCharacterType(CharacterType newType)
    {
        m_characterType = newType;
    }

    void hkpCharacterContext::setFilterEnable(const hkBool status)
    {
        m_filterEnable = status;
    }

    void hkpCharacterContext::setFilterParameters(const hkReal gain, const hkReal maxVelocity, const hkReal maxAcceleration)
    {
        m_gain                  = gain;
        m_maxLinearVelocity     = maxVelocity;
        m_maxLinearAcceleration = maxAcceleration;
    }
} }
