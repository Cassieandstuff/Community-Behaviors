/// @file
/// @brief hkbCharacterControllerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/CharacterController/hkbCharacterControllerModifier.h>

#include <Behavior/Driver/CharacterController/hkbCharacterControllerDriver.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Common/Base/Monitor/hkMonitorStream.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace havok
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

    hkbCharacterControllerModifierInternalState::hkbCharacterControllerModifierInternalState(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }
} }
