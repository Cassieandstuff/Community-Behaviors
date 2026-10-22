/// @file
/// @brief bhkCharacterStateOnGround::Simulate, still being matched (see tools/cbmatch/parked.yaml).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkPickData.h>
#include <Physics/Utilities/CharacterControl/Bethesda/Setting.h>
#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterStateManager.h>
#include <Physics/Utilities/CharacterControl/StateMachine/Util/hkpCharacterMovementUtil.h>

namespace CB { namespace havok
{
    /// @brief How far (Havok units, 64 game units) a fake-supported character moves before OnGround drops the fake
    ///        support.
    /// @note An object in vanilla's .rdata (0x1419f7340), compared in place.
    static const hkReal s_fakeSupportReleaseDistance = 0.9144f;

    /// @note Without support (kSupport clear), no fake support and no request for another state, the character
    ///       leaves the ground: into the air, simulated there at once, the fall starting at its height. With
    ///       bUseUnsupportedCast:HAVOK it first casts fUnsupportCastLength:HAVOK straight down; a hit keeps it grounded
    ///       for this step under fUnsupportedGravMult:HAVOK times gravity (and sets kAllowJumpNoContact when the hit is
    ///       steeper than m_zNormal). Grounded, the velocity is steered along the support surface; moving away from
    ///       the surface, it is turned back along it (the slope projection).
    void bhkCharacterStateOnGround::Simulate(bhkCharacterController* controller)
    {
        hkVector4 gravity = controller->GetHavokWorld()->GetGravity();
        if (!controller->IsFlagSet(8) && !controller->m_fakeSupport
            && (controller->m_wantState == controller->m_context.getState() || controller->m_wantState == HK_CHARACTER_MAX_STATE_ID)) {
            hkVector4 position;
            controller->GetPositionImpl(position, true);
            hkVector4 castTo = position;
            castTo(2) -= fUnsupportCastLength_HAVOK.m_value.f;
            const CFilter filter = controller->GetCollisionFilterInfo();
            bhkPickData   pickData;
            pickData.m_rayInput.m_from       = position;
            pickData.m_rayInput.m_to         = castTo;
            pickData.m_rayInput.m_filterInfo = filter.m_filter;
            if (bUseUnsupportedCast_HAVOK.m_value.b) {
                controller->GetHavokWorld()->PickObject(pickData);
                if (pickData.m_rayOutput.hasHit()) {
                    gravity.mul4(hkSimdReal(fUnsupportedGravMult_HAVOK.m_value.f));
                    if (pickData.m_rayOutput.m_normal(2) < controller->m_zNormal) controller->m_flags |= 0x10;
                    goto onGround;
                }
            }
            if (m_killVelocityOnLaunch) controller->m_outVelocity(2) = 0.0f;
            controller->m_wantState       = HK_CHARACTER_IN_AIR;
            controller->m_fallStartHeight = position(2) * bhkWorld::s_worldScaleInverse;
            controller->CommitWantedState();
            const hkpCharacterStateManager* manager = controller->m_context.m_stateManager;
            static_cast<bhkCharacterState*>(manager->getState(controller->m_context.getState()))->Simulate(controller);
            return;
        }

    onGround:
        if (controller->m_fakeSupport) {
            const hkVector4 start = controller->m_fakeSupportStart;
            hkVector4       position;
            controller->GetPositionImpl(position, true);
            hkVector4 moved;
            moved.setSub4(position, start);
            if (_mm_comigt_ss(moved.length3().m_real, _mm_load_ss(&s_fakeSupportReleaseDistance)) || controller->m_wantState != HK_CHARACTER_MAX_STATE_ID) controller->m_fakeSupport = false;
        }

        hkpMovementUtilInput input;
        input.m_forward         = controller->m_forward;
        input.m_up              = g_characterUp;
        input.m_surfaceNormal   = controller->m_supportNorm;
        input.m_currentVelocity = controller->m_outVelocity;
        input.m_gain            = 1.0f;
        const hkVector4& velocityMod = controller->m_velocityMod;
        if (controller->IsFlagSet(11)) {
            input.m_desiredVelocity.set(velocityMod.getComponent<1>(), velocityMod.getComponent<0>(), hkSimdReal(velocityMod.getComponent<2>().getReal()),
                                        hkSimdReal(_mm_setzero_ps()));
        } else {
            input.m_desiredVelocity.set(velocityMod.getComponent<1>(), velocityMod.getComponent<0>(), hkSimdReal(_mm_setzero_ps()),
                                        hkSimdReal(_mm_setzero_ps()));
        }
        input.m_maxVelocityDelta = 500.0f;
        input.m_surfaceVelocity  = controller->m_surfaceInfo.m_surfaceVelocity;
        hkpCharacterMovementUtil::calculateMovement(input, controller->m_outVelocity);
        if (!controller->IsFlagSet(11)) {
            controller->m_outVelocity(2) = input.m_currentVelocity(2);
            controller->m_outVelocity.addMul4(hkSimdReal(controller->m_stepInfo.m_deltaTime), gravity);
        }
        controller->ApplyInitialVelocity();
        if (controller->m_wantState != HK_CHARACTER_MAX_STATE_ID && (controller->m_wantState != HK_CHARACTER_JUMPING || controller->IsFlagSet(10)))
            controller->CommitWantedState();

        hkVector4& velocity = controller->m_outVelocity;
        velocity.sub4(controller->m_surfaceInfo.m_surfaceVelocity);
        const hkVector4& up = input.m_up;
        if (up.dot3(velocity).getReal() > 0.001f) {
            const hkVector4& normal      = input.m_surfaceNormal;
            const hkSimdReal normalDotUp = normal.dot3(up);
            if (hkMath::fabs(normalDotUp.getReal()) > HK_REAL_EPSILON) {
                const hkSimdReal lengthSquared = velocity.lengthSquared3();
                const hkSimdReal invLength     = hkMath::sqrtInverseOfSplat(lengthSquared);
                const hkSimdReal speed         = hkSimdReal(_mm_mul_ss(lengthSquared.m_real, invLength.m_real));
                hkVector4        direction;
                direction.setMul4(invLength, velocity);
                hkVector4        side;
                side.setCross(normal, direction);
                velocity.setCross(side, up);
                velocity.mul4(hkSimdReal(speed.getReal() / normalDotUp.getReal()));
            }
        }
        velocity.setAdd4(controller->m_surfaceInfo.m_surfaceVelocity, velocity);
    }
} }
