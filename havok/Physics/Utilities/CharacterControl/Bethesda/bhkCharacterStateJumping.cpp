/// @file
/// @brief bhkCharacterStateJumping, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterStateManager.h>

namespace CB { namespace havok
{
    bhkCharacterStateJumping::bhkCharacterStateJumping()
    {
    }

    hkpCharacterStateType bhkCharacterStateJumping::getType() const
    {
        return HK_CHARACTER_JUMPING;
    }

    /// @note A jump lasts one step: the launch velocity (the current horizontal velocity plus sqrt(2 g h) against
    ///       gravity, h = |m_jumpHeight|) is set, the fall starts at the current height, and the character is put in
    ///       the air (kSupport cleared) and simulated there at once. A request for the ground or climbing cancels it.
    void bhkCharacterStateJumping::Simulate(bhkCharacterController* controller)
    {
        if (controller->m_wantState != HK_CHARACTER_MAX_STATE_ID) {
            switch (controller->m_wantState) {
                case HK_CHARACTER_ON_GROUND:
                case HK_CHARACTER_CLIMBING: controller->CommitWantedState(); return;
            }
        }

        controller->m_flags |= 0x2000;
        const hkVector4& worldGravity = controller->GetHavokWorld()->GetGravity();
        hkVector4        down;
        down.setMul4(hkSimdReal(controller->m_gravity), worldGravity);
        const float      jumpHeight = controller->m_jumpHeight;
        const hkSimdReal gravity    = down.normalizeWithLength3OfSplat();
        hkVector4        velocity;
        controller->GetLinearVelocityImpl(velocity);
        velocity(2) = 0.0f;
        const float speed = -::sqrtf(hkMath::fabs(jumpHeight) * (gravity.getReal() * 2.0f));
        controller->m_outVelocity.setAddMul4(velocity, down, hkSimdReal(speed));

        hkVector4 position;
        controller->GetPositionImpl(position, true);
        controller->m_wantState       = HK_CHARACTER_IN_AIR;
        controller->m_fallStartHeight = position(2) * bhkWorld::s_worldScaleInverse;
        controller->CommitWantedState();
        controller->m_flags &= ~0x100u;
        const hkpCharacterStateManager* manager = controller->m_context.m_stateManager;
        static_cast<bhkCharacterState*>(manager->getState(controller->m_context.getState()))->Simulate(controller);
    }
} }
