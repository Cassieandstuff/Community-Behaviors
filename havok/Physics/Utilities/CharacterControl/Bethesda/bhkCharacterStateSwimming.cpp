/// @file
/// @brief bhkCharacterStateSwimming, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/StateMachine/Util/hkpCharacterMovementUtil.h>

namespace CB { namespace havok
{
    /// @brief How far (Havok units, 10 game units) below the water surface a surfaced swimmer sinks before gravity is
    ///        turned against it.
    /// @note An object in vanilla's .rdata (0x1419f73c0).
    static const hkReal s_surfaceSinkDepth = 0.142875f;

    bhkCharacterStateSwimming::bhkCharacterStateSwimming()
    {
    }

    hkpCharacterStateType bhkCharacterStateSwimming::getType() const
    {
        return HK_CHARACTER_USER_STATE_0;
    }

    void bhkCharacterStateSwimming::Simulate(bhkCharacterController* controller)
    {
        hkVector4&           velocity = controller->m_outVelocity;
        const hkReal         zero     = 0.0f;
        hkpMovementUtilInput input;
        input.m_forward         = controller->m_forward;
        input.m_up              = g_characterUp;
        input.m_currentVelocity = controller->m_outVelocity;
        const hkVector4 velocityMod = controller->m_velocityMod;
        input.m_desiredVelocity.set(velocityMod.getComponent<1>(), velocityMod.getComponent<0>(), hkSimdReal(velocityMod.getComponent<2>().getReal()),
                                    hkSimdReal(_mm_setzero_ps()));
        input.m_surfaceVelocity  = controller->m_surfaceInfo.m_surfaceVelocity;
        input.m_gain             = 1.0f;
        input.m_maxVelocityDelta = 100.0f;
        if (controller->IsFlagSet(8)) input.m_surfaceNormal = controller->m_supportNorm;
        else                          input.m_surfaceNormal = g_characterUp;
        hkpCharacterMovementUtil::calculateMovement(input, controller->m_outVelocity);

        hkVector4 position;
        controller->GetPositionImpl(position, false);
        float height = position.getComponent<2>().getReal() - controller->m_centerOffset(2);
        if (controller->m_swimFloatHeight >= 0.5f) height += controller->m_actorHeight * 0.5f * controller->m_swimFloatHeight;
        const hkVector4 gravity = controller->GetHavokWorld()->GetGravity();

        if (controller->m_swimFloatHeight < 0.5f) {
            if (height > controller->m_waterHeight - 5.0f) controller->m_flags |= 0x400;
            if (controller->m_wantState == HK_CHARACTER_JUMPING && controller->IsFlagSet(10)) {
                controller->CommitWantedState();
            } else if (height < controller->m_waterHeight - s_surfaceSinkDepth) {
                hkVector4 g;
                g.setMul4(hkSimdReal(controller->m_gravity), gravity);
                controller->m_outVelocity.addMul4(g, hkSimdReal(-controller->m_stepInfo.m_deltaTime));
            }
        } else {
            float gravityMult;
            if (!controller->IsFlagSet(23)) {
                controller->m_flags |= 0x400;
                if (controller->m_wantState == HK_CHARACTER_JUMPING) {
                    controller->CommitWantedState();
                    goto done;
                }
                if (velocity(2) > zero) controller->m_outVelocity(2) = 0.0f;
                gravityMult = controller->m_gravity;
            } else {
                if (controller->IsFlagSet(31) && velocity(2) < zero) {
                    controller->m_outVelocity(2) = 0.0f;
                    goto done;
                }
                gravityMult = controller->m_gravity;
                if (gravityMult == 1.0f || (controller->m_flags & 0x300)) goto done;
            }
            hkVector4 g;
            g.setMul4(hkSimdReal(gravityMult), gravity);
            controller->m_outVelocity.addMul4(g, hkSimdReal(controller->m_stepInfo.m_deltaTime));
        }

    done:
        controller->ApplyInitialVelocity();
        if (controller->m_wantState == HK_CHARACTER_ON_GROUND || controller->m_wantState == HK_CHARACTER_IN_AIR) controller->CommitWantedState();
    }
} }
