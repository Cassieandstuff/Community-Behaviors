/// @file
/// @brief hkbCharacterControllerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/CharacterController/hkbCharacterControllerModifier.h>

#include <Behavior/Driver/CharacterController/hkbCharacterControllerDriver.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Common/Base/Monitor/hkMonitorStream.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    hkbCharacterControllerModifier::hkbCharacterControllerModifier()
    :   m_initialVelocity(hkVector4::getZero()),
        m_initialVelocityCoordinates(INITIAL_VELOCITY_IN_WORLD_COORDINATES),
        m_motionMode(MOTION_MODE_FOLLOW_ANIMATION),
        m_forceDownwardMomentum(true),
        m_applyGravity(true),
        m_setInitialVelocity(false),
        m_isTouchingGround(true),
        m_gravity(hkVector4::getZero()),
        m_timestep(0.0f),
        m_isInitialVelocityAdded(false)
    {
    }

    hkbCharacterControllerModifier::hkbCharacterControllerModifier(const hkbCharacterControllerModifier& mod)
    :   hkbModifier(mod),
        m_gravity(hkVector4::getZero()),
        m_timestep(0.0f),
        m_isInitialVelocityAdded(false)
    {
        hkString::memCpy(&m_controlData, &mod.m_controlData, 0x36);
    }

    /// @note Vanilla asks the context for the world before each use (four calls).
    void hkbCharacterControllerModifier::activate(const hkbContext& context)
    {
        if (context.getWorld()) {
            context.getWorld()->lock();
            m_gravity = context.getWorld()->getGravity();
            context.getWorld()->unlock();
        }
        m_isInitialVelocityAdded = false;
    }

    void hkbCharacterControllerModifier::update(const hkbContext& context, hkReal timestep)
    {
        hkbCharacterControllerDriver* driver = context.m_character->m_characterControllerDriver;
        if (driver) {
            m_timestep += timestep;
            m_isTouchingGround = driver->isTouchingGround();
        }
    }

    hkbNode* hkbCharacterControllerModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbCharacterControllerModifier(*this);
    }

    hkReferencedObject* hkbCharacterControllerModifier::createInternalState()
    {
        return new hkbCharacterControllerModifierInternalState;
    }

    void hkbCharacterControllerModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbCharacterControllerModifierInternalState& is = static_cast<hkbCharacterControllerModifierInternalState&>(internalState);
        is.m_gravity                = m_gravity;
        is.m_timestep               = m_timestep;
        is.m_isInitialVelocityAdded = m_isInitialVelocityAdded;
        is.m_isTouchingGround       = m_isTouchingGround;
    }

    void hkbCharacterControllerModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbCharacterControllerModifierInternalState& is = static_cast<const hkbCharacterControllerModifierInternalState&>(internalState);
        m_gravity                = is.m_gravity;
        m_timestep               = is.m_timestep;
        m_isInitialVelocityAdded = is.m_isInitialVelocityAdded;
        m_isTouchingGround       = is.m_isTouchingGround;
    }

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

    hkbCharacterControllerModifierInternalState::hkbCharacterControllerModifierInternalState(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }
} }
