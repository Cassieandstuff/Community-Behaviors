/// @file
/// @brief hkpCharacterRigidBody, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBody.h>
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBodyCinfo.h>
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBodyListener.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace havok
{
    /// @note The body gets a zero inverse inertia (infinite inertia: contacts never rotate it) and no restitution.
    hkpCharacterRigidBody::hkpCharacterRigidBody(const hkpCharacterRigidBodyCinfo& info)
    {
        hkpRigidBodyCinfo ci;
        ci.m_mass                    = info.m_mass;
        ci.m_shape                   = info.m_shape;
        ci.m_collisionFilterInfo     = info.m_collisionFilterInfo;
        ci.m_position                = info.m_position;
        ci.m_rotation                = info.m_rotation;
        ci.m_friction                = info.m_friction;
        ci.m_restitution             = 0.0f;
        ci.m_maxLinearVelocity       = info.m_maxLinearVelocity;
        ci.m_allowedPenetrationDepth = info.m_allowedPenetrationDepth;
        ci.m_motionType              = hkpMotion::MOTION_CHARACTER;
        ci.m_qualityType             = HK_COLLIDABLE_QUALITY_CHARACTER;
        m_character = new hkpRigidBody(ci);

        hkMatrix3 zeroInvInertia;
        zeroInvInertia.setZero();
        m_character->setInertiaInvLocal(zeroInvInertia);
        m_character->setProperty(DEBUG_DISPLAY_COLOR_PROPERTY, hkpPropertyValue(info.m_vdbColor));

        m_up                          = info.m_up;
        m_maxSlopeCosine              = ::cosf(info.m_maxSlope);
        m_supportDistance             = info.m_supportDistance;
        m_hardSupportDistance         = info.m_hardSupportDistance;
        m_maxSpeedForSimplexSolver    = info.m_maxSpeedForSimplexSolver;
        m_unweldingHeightOffsetFactor = info.m_unweldingHeightOffsetFactor;
        m_acceleration.setZero4();
        m_maxForce = info.m_maxForce;

        m_character->addEntityListener(this);
        m_listener = HK_NULL;
    }

    hkpCharacterRigidBody::~hkpCharacterRigidBody()
    {
        if (m_listener) m_listener->removeReference();
        m_character->removeEntityListener(this);
        m_character->removeProperty(DEBUG_DISPLAY_COLOR_PROPERTY);
        m_character->removeReference();
    }

    void hkpCharacterRigidBody::checkSupport(const hkStepInfo& stepInfo, hkpSurfaceInfo& ground)
    {
        HK_TIMER_BEGIN("checkSupport");
        hkLocalArray<SupportInfo> supportInfo(20);
        ground.m_supportedState = static_cast<hkpSurfaceInfo::SupportedState>(getSupportInfo(stepInfo, supportInfo));
        if (ground.m_supportedState != hkpSurfaceInfo::UNSUPPORTED) getGround(supportInfo, false, ground);
        HK_TIMER_END();
    }

    /// @note Keyframed bodies always lend their point velocity; dynamic ones mark the surface dynamic (its distance
    ///       excess then a fixed 0.01) and lend it only with `useDynamicBodyVelocities`; fixed ones neither.
    void hkpCharacterRigidBody::getGround(const hkArray<SupportInfo>& supportInfo, hkBool useDynamicBodyVelocities, hkpSurfaceInfo& ground) const
    {
        HK_TIMER_BEGIN("getGround");
        ground.m_surfaceVelocity.setZero4();
        ground.m_surfaceNormal.setZero4();
        ground.m_surfaceDistanceExcess = 0.0f;
        ground.m_surfaceIsDynamic      = false;

        const int numSupportInfo = supportInfo.getSize();
        for (int i = 0; i < numSupportInfo; i++) {
            const SupportInfo& support = supportInfo[i];
            ground.m_surfaceNormal.add4(support.m_point.m_separatingNormal);
            ground.m_surfaceDistanceExcess += support.m_point.getDistance();

            const hkpMotion::MotionType motionType = support.m_rigidBody->getMotionType();
            if (motionType != hkpMotion::MOTION_KEYFRAMED) {
                if (motionType == hkpMotion::MOTION_FIXED) continue;
                ground.m_surfaceIsDynamic = true;
                if (!useDynamicBodyVelocities) continue;
            }
            hkVector4 pointVelocity;
            support.m_rigidBody->getPointVelocity(support.m_point.m_position, pointVelocity);
            ground.m_surfaceVelocity.add4(pointVelocity);
        }

        ground.m_surfaceNormal.normalize3();
        const hkReal invNumSupportInfo = 1.0f / static_cast<hkReal>(numSupportInfo);
        ground.m_surfaceVelocity.mul4(hkSimdReal(invNumSupportInfo));
        ground.m_surfaceDistanceExcess *= invNumSupportInfo;
        if (ground.m_surfaceIsDynamic) ground.m_surfaceDistanceExcess = 0.01f;
        else                           ground.m_surfaceDistanceExcess -= m_hardSupportDistance;
        HK_TIMER_END();
    }

    void hkpCharacterRigidBody::entityAddedCallback(hkpEntity* entity)
    {
        addReference();
        if (m_listener) entity->getWorld()->addWorldPostSimulationListener(this);
    }

    void hkpCharacterRigidBody::entityRemovedCallback(hkpEntity* entity)
    {
        if (m_listener) entity->getWorld()->removeWorldPostSimulationListener(this);
        removeReference();
    }

    void hkpCharacterRigidBody::postSimulationCallback(hkpWorld* world)
    {
        m_listener->characterCallback(world, this);
    }

    hkpRigidBody* hkpCharacterRigidBody::getRigidBody() const
    {
        return m_character;
    }

    void hkpCharacterRigidBody::setLinearAccelerationToMassModifier(const hkVector4& newAcc)
    {
        m_acceleration = newAcc;
    }

    void hkpCharacterRigidBody::setLinearVelocity(const hkVector4& newVel, hkReal timestep)
    {
        hkVector4 velocityChange;
        velocityChange.setSub4(newVel, m_character->getLinearVelocity());
        if (!newVel.equals3(m_character->getLinearVelocity(), 0.001f)) {
            hkVector4 acceleration;
            acceleration.setMul4(hkSimdReal(1.0f / timestep), velocityChange);
            setLinearAccelerationToMassModifier(acceleration);
            m_character->setLinearVelocity(newVel);
        }
    }

    const hkVector4& hkpCharacterRigidBody::getLinearVelocity() const
    {
        return m_character->getLinearVelocity();
    }

    void hkpCharacterRigidBody::setAngularVelocity(const hkVector4& newAVel)
    {
        if (!newAVel.equals3(m_character->getAngularVelocity(), 0.001f)) m_character->setAngularVelocity(newAVel);
    }
} }
