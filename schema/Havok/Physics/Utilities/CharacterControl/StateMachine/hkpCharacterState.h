/// @file
/// @brief hkpCharacterState: one state of the character controller's state machine (on ground, jumping, in air, ...).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpCharacterContext;
    struct hkpCharacterInput;
    struct hkpCharacterOutput;

    /// @brief The state ids a state manager registers states under.
    /// @note Values per the Havok SDK; HK_CHARACTER_MAX_STATE_ID (11) is proven by hkpCharacterStateManager's table,
    ///       HK_CHARACTER_JUMPING (1) by hkpCharacterContext::update (no acceleration cap while jumping).
    enum hkpCharacterStateType
    {
        HK_CHARACTER_ON_GROUND    = 0,
        HK_CHARACTER_JUMPING      = 1,
        HK_CHARACTER_IN_AIR       = 2,
        HK_CHARACTER_CLIMBING     = 3,
        HK_CHARACTER_FLYING       = 4,
        HK_CHARACTER_USER_STATE_0 = 5,
        HK_CHARACTER_USER_STATE_1 = 6,
        HK_CHARACTER_USER_STATE_2 = 7,
        HK_CHARACTER_USER_STATE_3 = 8,
        HK_CHARACTER_USER_STATE_4 = 9,
        HK_CHARACTER_USER_STATE_5 = 10,
        HK_CHARACTER_MAX_STATE_ID = 11,
    };

    /// @note 0x10 bytes (no members of its own). Vtable per vanilla 0x1419e4268: slots 3, 6 and 7 pure; enterState and
    ///       leaveState empty. Slots 4..7 are proven by hkpCharacterContext's calls.
    class hkpCharacterState : public hkReferencedObject
    {
    public:
        virtual hkpCharacterStateType getType() const = 0;   ///< slot 3
        virtual void enterState(hkpCharacterContext& context, hkpCharacterStateType prevState, const hkpCharacterInput& input,
                                hkpCharacterOutput& output);   ///< slot 4
        virtual void leaveState(hkpCharacterContext& context, hkpCharacterStateType nextState, const hkpCharacterInput& input,
                                hkpCharacterOutput& output);   ///< slot 5
        /// @brief This step's behaviour in the state.
        virtual void update(hkpCharacterContext& context, const hkpCharacterInput& input, hkpCharacterOutput& output) = 0;   ///< slot 6
        /// @brief The transitions out of the state.
        virtual void change(hkpCharacterContext& context, const hkpCharacterInput& input, hkpCharacterOutput& output) = 0;   ///< slot 7
    };
} }
