/// @file
/// @brief bhkCharacterController's per-frame update (UpdatePhysics) and its helpers, still being matched.
/// @note Vanilla compiles them into bhkCharacterController's own unit (0x140e9dcd0..0x140ea3b50).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/Bethesda/NiMatrix3.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCollisionBox.h>
#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterStateManager.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    float bhkCharacterController::UpdateHeading(float heading, float deltaTime)
    {
        if (m_rotModTime > 0.0f) {
            const float turned = hkMath::min2(m_rotModTime, deltaTime) * m_rotMod + heading;
            m_rotModTime -= deltaTime;
            m_rotMod -= 0.05f;
            return turned;
        }
        m_rotMod = 0.0f;
        return heading;
    }

    namespace
    {
        /// @note By value, with w stored first: vanilla's temporaries for a vector built from three floats.
        HK_FORCE_INLINE hkVector4 MakeVector3(float x, float y, float z)
        {
            hkVector4 v;
            v(3) = 0.0f;
            v(0) = x;
            v(1) = y;
            v(2) = z;
            return v;
        }
        /// @note By value: vanilla builds the columns in a temporary (only their w stored to it) and copies it.
        HK_FORCE_INLINE hkRotation MakeRotation(const NiMatrix3& m)
        {
            hkRotation r;
            r.m_col0(0) = m.m_entry[0][0];
            r.m_col0(1) = m.m_entry[1][0];
            r.m_col0(2) = m.m_entry[2][0];
            r.m_col0(3) = 0.0f;
            r.m_col1(0) = m.m_entry[0][1];
            r.m_col1(1) = m.m_entry[1][1];
            r.m_col1(2) = m.m_entry[2][1];
            r.m_col1(3) = 0.0f;
            r.m_col2(0) = m.m_entry[0][2];
            r.m_col2(1) = m.m_entry[1][2];
            r.m_col2(2) = m.m_entry[2][2];
            r.m_col2(3) = 0.0f;
            return r;
        }
    }

    hkUint32 bhkCharacterController::UpdatePhysics(bhkCharacterMoveData& data)
    {
        MemoryContextScope context(0x36);
        if (m_context.getState() == HK_CHARACTER_FLYING) {
            HK_TIMER_BEGIN("update character state");
            const NiPoint3 tilt(data.m_pitch, data.m_roll, 0.0f);
            m_pitchAngle = tilt.x;
            m_rollAngle  = tilt.y;
            hkTransform transform;
            GetTransformImpl(transform);
            const hkVector4 c0 = transform.m_rotation.m_col0;
            const hkVector4 c1 = transform.m_rotation.m_col1;
            const hkVector4 c2 = transform.m_rotation.m_col2;
            const NiPoint3& m  = data.m_movement;
            data.m_movement = NiPoint3(c0(0) * m.x + c1(0) * m.y + c2(0) * m.z, c0(1) * m.x + c1(1) * m.y + c2(1) * m.z,
                                       c0(2) * m.x + c1(2) * m.y + c2(2) * m.z);
            const NiPoint3 scaled(data.m_movement.x * 0.0142875f, data.m_movement.y * 0.0142875f,
                                  data.m_movement.z * 0.0142875f);

            hkVector4 translation;
            translation.setAdd4(transform.m_translation, MakeVector3(scaled.x, scaled.y, scaled.z));
            NiMatrix3 axes;
            axes.EulerAnglesToAxesZXY(data.m_heading, data.m_pitch, data.m_roll);

            transform.m_translation     = translation;
            transform.m_rotation.m_col0 = MakeVector3(axes.m_entry[0][0], axes.m_entry[1][0], axes.m_entry[2][0]);
            transform.m_rotation.m_col1 = MakeVector3(axes.m_entry[0][1], axes.m_entry[1][1], axes.m_entry[2][1]);
            transform.m_rotation.m_col2 = MakeVector3(axes.m_entry[0][2], axes.m_entry[1][2], axes.m_entry[2][2]);
            SetTransformImpl(transform);
            static_cast<bhkCharacterState*>(m_context.m_stateManager->getState(m_context.getState()))->Simulate(this);
            const float dt = data.m_deltaTime;
            m_gameVelocity = dt > 0.0f ? NiPoint3((1.0f / dt) * data.m_movement.x, (1.0f / dt) * data.m_movement.y,
                                                    (1.0f / dt) * data.m_movement.z)
                                       : NiPoint3::ZERO;
            m_outVelocity(0) = m_gameVelocity.x * 0.0142875f;
            m_outVelocity(1) = m_gameVelocity.y * 0.0142875f;
            m_outVelocity(2) = m_gameVelocity.z * 0.0142875f;
            m_outVelocity(3) = 0.0f;
            Unk_0C();
            HK_TIMER_END();
            return 0;
        }

        m_flags &= ~0x90u;
        ClearDamageImpacts();
        m_bumpedBody                = HK_NULL;
        m_bumpedForce               = 0.0f;
        m_bumpedCharCollisionObject = HK_NULL;
        m_speedPct = data.m_speedPct / s_speedPctDivisor;
        const float deltaTime = data.m_deltaTime;
        bhkWorld*   world     = GetHavokWorld();
        if (!(data.m_deltaTime > 1e-4f) || !world) return 0;

        if (IsFlagSet(22)) {
            if (GetHavokWorld()->m_unkc5ec > 0) return 0;
            m_flags &= ~0x400000u;
        }
        bool singleStep = IsFlagSet(20);
        if (!singleStep && !IsFlagSet(2)) {
            singleStep = data.m_movement.x * data.m_movement.x + data.m_movement.y * data.m_movement.y
                             + data.m_movement.z * data.m_movement.z
                         < s_singleStepLengthSq;
        }
        bool stationary;
        if (hkMath::fabs(data.m_movement.x - NiPoint3::ZERO.x) > 0.05f || hkMath::fabs(data.m_movement.y - NiPoint3::ZERO.y) > 0.05f
            || hkMath::fabs(data.m_movement.z - NiPoint3::ZERO.z) > 0.05f) {
            if (m_pushCount) {
                m_pushDelta.setZero4();
                m_pushCount = 0;
            }
            m_rotMod     = 0.0f;
            m_rotModTime = 0.0f;
            stationary   = false;
            m_flags |= 0x8;
        } else if (m_initialVelocity.equals3(hkVector4::getZero(), 0.001f)) {
            m_flags &= ~0x8u;
            stationary = true;
        } else {
            stationary = false;
            m_flags |= 0x8;
        }

        hkUint32 result = 0;
        HK_TIMER_BEGIN("Character movement");
        if (!IsFlagSet(17)) {
            hkpRigidBody* rigidBody = hkpGetRigidBody(GetRigidBody()->getCollidable());
            int           steps     = rigidBody ? 1 : 1 - static_cast<int>(data.m_deltaTime * -20.0f);
            if (!stationary && steps > 1 && !singleStep) {
                if (steps > 2) steps = 2;
                const float inv = 1.0f / static_cast<float>(steps);
                data.m_movement.x *= inv;
                data.m_movement.y *= inv;
                data.m_movement.z *= inv;
                data.m_deltaTime *= inv;
            } else {
                steps = 1;
            }
            UpdateSlopeTilt(deltaTime);
            if (!rigidBody) {
                m_surfaceMaterial = 0;
                m_contactLayer    = 0;
            }
            const float heading = UpdateHeading(data.m_heading, data.m_deltaTime);
            data.m_heading = heading;
            hkQuaternion orientation;
            orientation.setAxisAngle(g_characterUp, -heading);
            float stepTime;
            if (!rigidBody || (stepTime = bhkWorld::s_stepDeltaTime) == 0.0f) stepTime = data.m_deltaTime;
            m_stepInfo.m_deltaTime    = stepTime;
            m_stepInfo.m_invDeltaTime = 1.0f / stepTime;
            const float mx = data.m_movement.x;
            const float my = data.m_movement.y;
            float       mz = data.m_movement.z;
            if (m_context.getState() != HK_CHARACTER_USER_STATE_0 && !IsFlagSet(11)) mz = 0.0f;
            const float invDeltaTime = m_stepInfo.m_invDeltaTime;
            m_velocityMod(0) = invDeltaTime * mx * 0.0142875f;
            m_velocityMod(1) = invDeltaTime * my * 0.0142875f;
            m_velocityMod(2) = invDeltaTime * mz * 0.0142875f;
            m_velocityMod(3) = 0.0f;
            const hkpCharacterStateType state = m_context.getState();
            if (static_cast<hkUint32>(state - HK_CHARACTER_FLYING) <= 2 && hkMath::fabs(data.m_pitch) > 1e-4f) {
                hkQuaternion pitch;
                pitch.setAxisAngle(hkVector4(-1.0f, 0.0f, 0.0f, 0.0f), data.m_pitch);
                m_velocityMod.setRotatedDir(pitch, m_velocityMod);
            }
            m_centerOffset(2) = m_center;
            SetOrientation(orientation);
            hkVector4 forward;
            forward.setRotatedDir(orientation, hkVector4(0.0f, -1.0f, 0.0f, 0.0f));
            if (!stationary || !forward.equals3(m_forward, 0.001f)) m_calculatePitchTimer = s_calculatePitchTime;
            m_forward = forward;

            hkVector4& velocity = m_outVelocity;
            while (steps) {
                --steps;
                GetLinearVelocityImpl(velocity);
                if (stationary && m_context.getState() != HK_CHARACTER_IN_AIR) m_outVelocity(2) = 0.0f;
                m_gameVelocity.x = velocity(0) * 69.99125f;
                m_gameVelocity.y = velocity(1) * 69.99125f;
                m_gameVelocity.z = velocity(2) * 69.99125f;
                if (data.m_forceSupportCheck || m_context.getState() != HK_CHARACTER_ON_GROUND
                    || data.m_movement.x != NiPoint3::ZERO.x || data.m_movement.y != NiPoint3::ZERO.y
                    || data.m_movement.z != NiPoint3::ZERO.z
                    || (rigidBody && velocity.dot3(g_characterHorizontal).getReal() != 0.0f)
                    || m_supportNorm.equals4(hkVector4::getZero(), 0.001f)) {
                    m_flags &= ~0x200u;
                    m_flags |= 0x10000;
                    m_orientationCtrl->Unk_02(this);
                    CheckSupportImpl();
                    hkUint32 flags = m_flags & ~0x10000u;
                    m_flags        = flags;
                    const bool supported = m_surfaceInfo.m_supportedState == hkpSurfaceInfo::SUPPORTED && !m_ignoreSupport;
                    if (IsFlagSet(13)) {
                        if ((supported || IsFlagSet(9)) && m_outVelocity(2) > 0.0f) {
                            hkVector4        direction = velocity;
                            const hkSimdReal length    = direction.length3();
                            if (length.getReal() > 0.0f) {
                                direction.mul4(hkSimdReal(1.0f / length.getReal()));
                                if (supported && IsFlagSet(9) && m_supportNorm(2) >= m_zNormal
                                    && m_supportNorm.dot3(direction).getReal() <= 0.05f) {
                                    flags &= ~0x2000u;
                                }
                            }
                        } else {
                            flags &= ~0x2000u;
                        }
                        flags &= ~0x100u;
                    } else if (supported | IsFlagSet(9)) {
                        flags |= 0x100;
                    } else {
                        flags &= ~0x100u;
                    }
                    m_flags = flags;
                }
                HK_TIMER_BEGIN("update character state");
                static_cast<bhkCharacterState*>(m_context.m_stateManager->getState(m_context.getState()))->Simulate(this);
                HK_TIMER_END();
                result      = Unk_0B() ? 0 : 3;
                m_wantState = HK_CHARACTER_MAX_STATE_ID;
            }

            if (m_calculatePitchTimer > 0.0f && m_updatePitch
                && ((m_flags & 0x6000000) || m_pitchAngle != 0.0f || m_rollAngle != 0.0f)) {
                m_pitchMult = hkMath::min2(m_pitchMult, 1.0f);
                m_orientationCtrl->Unk_08(this);
            }
            if (IsFlagSet(7)) result = 2;
            if (m_unk270) static_cast<bhkCollisionBox*>(m_unk270.get())->UpdateFromController();
        } else {
            const float s  = sinf(data.m_heading);
            const float c  = cosf(data.m_heading);
            const float mx = data.m_movement.x;
            const float my = data.m_movement.y;
            data.m_movement = NiPoint3(mx * c + my * s, my * c - mx * s, data.m_movement.z);
            hkVector4 position;
            GetPositionImpl(position, true);
            const hkVector4 current  = position;
            const NiPoint3& movement = data.m_movement;
            const float heading = UpdateHeading(data.m_heading, data.m_deltaTime);
            data.m_heading = heading;
            hkQuaternion orientation;
            orientation.setAxisAngle(g_characterUp, -heading);
            SetOrientation(orientation);
            hkVector4 moved;
            moved(0) = (current(0) * 69.99125f + movement.x) * 0.0142875f;
            moved(1) = (current(1) * 69.99125f + movement.y) * 0.0142875f;
            moved(2) = (current(2) * 69.99125f + movement.z) * 0.0142875f;
            moved(3) = 0.0f;
            SetPositionImpl(moved, true, false);
        }
        Unk_0C();
        HK_TIMER_END();

        if (m_velocityTime > 0.0f) {
            m_velocityTime -= deltaTime;
            if (m_velocityTime <= 0.0f) {
                m_velocityTime = 0.0f;
                m_initialVelocity.setZero4();
            }
        }
        if (m_calculatePitchTimer > 0.0f) m_calculatePitchTimer -= deltaTime;
        m_flags &= ~0x1000u;
        return result;
    }
} }
