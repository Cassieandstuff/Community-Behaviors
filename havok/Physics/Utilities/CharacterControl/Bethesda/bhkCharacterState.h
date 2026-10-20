/// @file
/// @brief bhkCharacterState and its subclasses: Bethesda's character-controller states. Each step the controller runs
///        its current state's Simulate, which requests transitions through the controller's m_wantState and commits
///        them (bhkCharacterController::CommitWantedState), and writes the output velocity.
#pragma once

#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterState.h>

namespace CB { namespace havok
{
    class bhkCharacterController;

    /// @brief The character's up direction the states steer and fall by.
    /// @note Name provisional; a global set by a static initializer.
    extern hkVector4 g_characterUp;
    /// @brief The game's cached copies of the fJumpMoveMult / fJumpMoveBase game settings: InAir's steering gain is
    ///        acrobatics * fJumpMoveMult + fJumpMoveBase.
    /// @note Names provisional.
    extern float g_fJumpMoveMult;
    extern float g_fJumpMoveBase;

    /// @note 0x10 bytes; vtable 0x1419e42b0 (getType and Simulate pure).
    class bhkCharacterState : public hkpCharacterState
    {
    public:
        /// @note Havok's per-step pair is empty: Bethesda's controller runs Simulate instead.
        virtual void update(hkpCharacterContext& context, const hkpCharacterInput& input, hkpCharacterOutput& output) {}   ///< slot 6
        virtual void change(hkpCharacterContext& context, const hkpCharacterInput& input, hkpCharacterOutput& output) {}   ///< slot 7
        /// @brief One step of the state for `controller`: its transitions and its output velocity.
        /// @note Name provisional.
        virtual void Simulate(bhkCharacterController* controller) = 0;   ///< slot 8
    };

    /// @note 0x18 bytes; vtable 0x1419f72f8.
    class bhkCharacterStateOnGround : public bhkCharacterState
    {
    public:
        /// @note Out of line in vanilla (0x140f02520).
        bhkCharacterStateOnGround();

        virtual hkpCharacterStateType getType() const;          ///< slot 3
        virtual void Simulate(bhkCharacterController* controller);   ///< slot 8

        /// @brief Zero the vertical output velocity when the character leaves the ground.
        /// @note Name per the Havok SDK's hkpCharacterStateOnGround (true by default there, as here).
        hkBool m_killVelocityOnLaunch;   ///< +0x10
    };

    /// @note 0x10 bytes; vtable 0x1419f7130.
    class bhkCharacterStateJumping : public bhkCharacterState
    {
    public:
        /// @note Out of line in vanilla (0x140f02280).
        bhkCharacterStateJumping();

        virtual hkpCharacterStateType getType() const;          ///< slot 3
        virtual void Simulate(bhkCharacterController* controller);   ///< slot 8
    };

    /// @note 0x18 bytes; vtable 0x1419f70b8.
    class bhkCharacterStateInAir : public bhkCharacterState
    {
    public:
        /// @note Out of line in vanilla (0x140f01f00).
        bhkCharacterStateInAir();

        virtual hkpCharacterStateType getType() const;          ///< slot 3
        virtual void Simulate(bhkCharacterController* controller);   ///< slot 8

        /// @brief The steering's hkpMovementUtilInput::m_maxVelocityDelta (2000 by default).
        /// @note Name provisional.
        hkReal m_maxVelocityDelta;   ///< +0x10
    };

    /// @note 0x10 bytes; vtable 0x1419e4440. Vanilla inlines the constructor (into bhkCharacterControllerCinfo's).
    class bhkCharacterStateClimbing : public bhkCharacterState
    {
    public:
        virtual hkpCharacterStateType getType() const;          ///< slot 3
        virtual void Simulate(bhkCharacterController* controller);   ///< slot 8
    };

    /// @note 0x10 bytes; vtable 0x1419e4490. Vanilla inlines the constructor (into bhkCharacterControllerCinfo's).
    class bhkCharacterStateFlying : public bhkCharacterState
    {
    public:
        virtual hkpCharacterStateType getType() const;          ///< slot 3
        virtual void Simulate(bhkCharacterController* controller);   ///< slot 8
    };

    /// @note 0x10 bytes; vtable 0x1419f7378. Registered under HK_CHARACTER_USER_STATE_0.
    class bhkCharacterStateSwimming : public bhkCharacterState
    {
    public:
        /// @note Out of line in vanilla (0x140f02b00).
        bhkCharacterStateSwimming();

        virtual hkpCharacterStateType getType() const;          ///< slot 3
        virtual void Simulate(bhkCharacterController* controller);   ///< slot 8
    };
} }
