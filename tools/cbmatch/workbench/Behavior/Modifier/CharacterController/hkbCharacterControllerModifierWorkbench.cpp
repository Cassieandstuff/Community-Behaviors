/// @file
/// @brief Workbench: the functions of hkbCharacterControllerModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        havok/Behavior/Modifier/CharacterController/hkbCharacterControllerModifier.cpp.
#include <Behavior/Modifier/CharacterController/hkbCharacterControllerModifier.h>
#include <Behavior/Driver/CharacterController/hkbCharacterControllerDriver.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Common/Base/Monitor/hkMonitorStream.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace havok
{
    /// @note Following the animation, the desired velocity is the motion from the character's world-from-model to the
    ///       generated one (in the old frame, rotated by the generated rotation) over the accumulated timestep.
    void hkbCharacterControllerModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("CharacterControllerModifier");

        if (context.m_character->m_characterControllerDriver && inOut.isValid(hkbGeneratorOutput::TRACK_CHARACTER_CONTROLLER_CONTROL_DATA)) {
            m_controlData.m_desiredVelocity.setZero4();

            if (m_timestep > 0.0f) {
                hkbCharacterControllerDriver* driver = context.m_character->m_characterControllerDriver;
                m_isTouchingGround = driver->isTouchingGround();
                const hkVector4& velocity = driver->getLinearVelocity();

                if (m_motionMode == MOTION_MODE_DYNAMIC) {
                    const bool addInitialVelocity = m_setInitialVelocity && !m_isInitialVelocityAdded;
                    const bool addVelocity        = !m_setInitialVelocity || m_isInitialVelocityAdded;
                    if (addInitialVelocity) {
                        hkVector4 initialVelocity;
                        if (m_initialVelocityCoordinates == INITIAL_VELOCITY_IN_WORLD_COORDINATES) {
                            initialVelocity = m_initialVelocity;
                        }
                        else {
                            initialVelocity.setRotatedDir(inOut.accessWorldFromModel().m_rotation, m_initialVelocity);
                        }
                        m_controlData.m_desiredVelocity.add4(initialVelocity);
                        m_isInitialVelocityAdded = true;
                    }
                    if (addVelocity) {
                        m_controlData.m_desiredVelocity.add4(velocity);
                    }
                }
                else {
                    hkQsTransform motion;
                    motion.setMulInverseMul(*context.m_character->m_worldFromModel, inOut.accessWorldFromModel());
                    hkVector4 desiredVelocity;
                    desiredVelocity.setRotatedDir(inOut.accessWorldFromModel().m_rotation, motion.m_translation);
                    desiredVelocity.mul4(1.0f / m_timestep);
                    m_controlData.m_desiredVelocity = desiredVelocity;

                    if (m_forceDownwardMomentum) {
                        const hkVector4& up            = context.getWorldUpWS();
                        hkReal           verticalSpeed = velocity.dot3(up).getReal();
                        if (verticalSpeed >= 0.0f) verticalSpeed = 0.0f;
                        if (verticalSpeed < 0.0f) {
                            m_controlData.m_desiredVelocity.subMul4(m_controlData.m_desiredVelocity.dot3(up), up);
                            m_controlData.m_desiredVelocity.addMul4(hkSimdReal(verticalSpeed), up);
                        }
                    }
                }

                if (!m_isTouchingGround && m_applyGravity) {
                    m_controlData.m_desiredVelocity.addMul4(hkSimdReal(m_timestep), m_gravity);
                }
                m_timestep = 0.0f;
            }

            hkbGeneratorOutput::TrackHeader& header      = inOut.getTrackHeader(hkbGeneratorOutput::TRACK_CHARACTER_CONTROLLER_CONTROL_DATA);
            void*                           controlData = inOut.getTrackData(hkbGeneratorOutput::TRACK_CHARACTER_CONTROLLER_CONTROL_DATA);
            if (header.m_numData > 0) {
                hkString::memCpy4(controlData, &m_controlData, sizeof(hkbCharacterControllerControlData) / 4);
                header.m_onFraction = 1.0f;
            }
        }

        HK_TIMER_END();
    }

} }
