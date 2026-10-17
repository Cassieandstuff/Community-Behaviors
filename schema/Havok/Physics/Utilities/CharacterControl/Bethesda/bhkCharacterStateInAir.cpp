/// @file
/// @brief bhkCharacterStateInAir, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/Bethesda/Setting.h>
#include <Physics/Utilities/CharacterControl/StateMachine/Util/hkpCharacterMovementUtil.h>

namespace CB { namespace schema
{
    bhkCharacterStateInAir::bhkCharacterStateInAir()
    :   m_maxVelocityDelta(2000.0f)
    {
    }

    hkpCharacterStateType bhkCharacterStateInAir::getType() const
    {
        return HK_CHARACTER_IN_AIR;
    }

    /// @note Support lands the character. Without support, a character that has been in the air over a second
    ///       without falling (less than 0.1 game units below its highest point) is stuck: it is put on the ground and
    ///       m_fakeSupport set. A jump request is dropped. While steering, the vertical velocity is kept as it was.
    void bhkCharacterStateInAir::Simulate(bhkCharacterController* controller)
    {
        float     gravityMult = 1.0f;
        hkVector4 position;
        if (controller->IsFlagSet(8)) {
            controller->m_wantState = HK_CHARACTER_ON_GROUND;
            controller->m_flags |= 0x80;
        } else if (controller->m_wantState == HK_CHARACTER_MAX_STATE_ID && controller->m_fallTime > gravityMult
                   && controller->GetFallDistance() < 0.1f) {
            controller->m_wantState   = HK_CHARACTER_ON_GROUND;
            controller->m_fakeSupport = true;
            controller->GetPositionImpl(position, true);
            controller->m_fakeSupportStart = position;
        }

        if (controller->m_wantState != HK_CHARACTER_MAX_STATE_ID) {
            if (controller->m_wantState != HK_CHARACTER_JUMPING) {
                controller->m_fallTime = 0.0f;
                controller->CommitWantedState();
                return;
            }
            controller->m_wantState = HK_CHARACTER_MAX_STATE_ID;
        }

        if (controller->m_acrobatics > 0.0f) {
            hkpMovementUtilInput input;
            if (controller->m_flags & 0x1800) input.m_gain = 1.0f;
            else                              input.m_gain = controller->m_acrobatics * g_fJumpMoveMult + g_fJumpMoveBase;
            input.m_forward          = controller->m_forward;
            input.m_up               = g_characterUp;
            input.m_surfaceNormal    = g_characterUp;
            input.m_currentVelocity  = controller->m_outVelocity;
            const hkVector4 velocityMod = controller->m_velocityMod;
            input.m_desiredVelocity.set(velocityMod.getComponent<1>(), velocityMod.getComponent<0>(), hkSimdReal(_mm_setzero_ps()),
                                        hkSimdReal(_mm_setzero_ps()));
            input.m_surfaceVelocity  = controller->m_surfaceInfo.m_surfaceVelocity;
            input.m_maxVelocityDelta = m_maxVelocityDelta;
            hkpCharacterMovementUtil::calculateMovement(input, controller->m_outVelocity);

            controller->m_outVelocity.addMul4(-controller->m_outVelocity.dot3(g_characterUp), g_characterUp);
            controller->m_outVelocity.addMul4(input.m_currentVelocity.dot3(g_characterUp), g_characterUp);
        }

        controller->GetPositionImpl(position, true);
        const float height = position(2) * bhkWorld::s_worldScaleInverse;
        if (height > controller->m_fallStartHeight) controller->m_fallStartHeight = height;
        else                                         gravityMult = fInAirFallingCharGravityMult_HAVOK.m_value.f;

        if (controller->GetHavokWorld()) {
            const hkVector4& gravity = controller->GetHavokWorld()->GetGravity();
            hkVector4        g;
            g.setMul4(hkSimdReal(gravityMult * controller->m_gravity), gravity);
            controller->m_outVelocity.addMul4(g, hkSimdReal(controller->m_stepInfo.m_deltaTime));
        }
        controller->AccumulateFallTime();
    }
} }
