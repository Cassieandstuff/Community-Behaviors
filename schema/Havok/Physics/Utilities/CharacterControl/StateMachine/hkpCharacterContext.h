/// @file
/// @brief hkpCharacterContext: a character's state machine instance (current state, the manager it runs, output
///        filtering), with the per-step input and output the states read and write.
#pragma once

#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterState.h>
#include <Common/Base/Types/Physics/hkStepInfo.h>

namespace CB { namespace schema
{
    class hkpCharacterStateManager;

    /// @brief What the states read each step.
    /// @note Not reflected; only what hkpCharacterContext::update reads is declared.
    struct hkpCharacterInput
    {
        hkUint8    m_pad00[0x80];
        hkStepInfo m_stepInfo;   ///< +0x80
        hkUint8    m_pad90[0xa0 - 0x90];
        /// @brief The character's current velocity.
        hkVector4  m_velocity;   ///< +0xa0
    };

    /// @brief What the states produce each step.
    /// @note Not reflected; only the velocity is declared.
    struct hkpCharacterOutput
    {
        hkVector4 m_velocity;   ///< +0x00
    };

    /// @note 0x38 bytes. Not reflected.
    class hkpCharacterContext : public hkReferencedObject
    {
    public:
        /// @brief Which controller the context drives.
        /// @note Values per the Havok SDK (the constructor stores HK_CHARACTER_PROXY, 0).
        enum CharacterType
        {
            HK_CHARACTER_PROXY     = 0,
            HK_CHARACTER_RIGIDBODY = 1,
        };

        hkEnum<CharacterType, hkInt32>  m_characterType;           ///< +0x10
        const hkpCharacterStateManager* m_stateManager;            ///< +0x18 — referenced
        hkpCharacterStateType           m_currentState;            ///< +0x20
        hkpCharacterStateType           m_previousState;           ///< +0x24
        hkBool                          m_filterEnable;            ///< +0x28
        hkReal                          m_maxLinearAcceleration;   ///< +0x2c
        hkReal                          m_maxLinearVelocity;       ///< +0x30
        hkReal                          m_gain;                    ///< +0x34

        hkpCharacterContext(const hkpCharacterStateManager* manager, hkpCharacterStateType initialState);
        ~hkpCharacterContext();

        hkpCharacterStateType getState() const;
        /// @brief Leaves the current state for `state` (its leaveState, then the new state's enterState).
        void setState(hkpCharacterStateType state, const hkpCharacterInput& input, hkpCharacterOutput& output);
        /// @brief Runs the current state's transitions, then its update if none fired; with the filter on, clamps
        ///        the velocity to m_maxLinearVelocity and eases it toward the target by m_gain, the acceleration
        ///        capped at m_maxLinearAcceleration except while jumping.
        void update(const hkpCharacterInput& input, hkpCharacterOutput& output);
        void setCharacterType(CharacterType newType);
        void setFilterEnable(const hkBool status);
        void setFilterParameters(const hkReal gain, const hkReal maxVelocity, const hkReal maxAcceleration);
    };
} }
