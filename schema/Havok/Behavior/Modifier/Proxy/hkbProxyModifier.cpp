/// @file
/// @brief hkbProxyModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Proxy/hkbProxyModifier.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Physics/Dynamics/Phantom/hkpCachingShapePhantom.h>
#include <Physics/Dynamics/Phantom/hkpSimpleShapePhantom.h>
#include <Physics/Dynamics/World/hkpWorld.h>
#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxy.h>

namespace CB { namespace schema
{
    hkbProxyModifier::hkbProxyModifier()
    :   m_linearVelocity(hkVector4::getZero()),
        m_horizontalGain(0.8f),
        m_verticalGain(0.2f),
        m_maxHorizontalSeparation(0.15f),
        m_maxVerticalSeparation(5.0f),
        m_verticalDisplacementError(0.0f),
        m_verticalDisplacementErrorGain(0.0f),
        m_maxVerticalDisplacement(0.5f),
        m_minVerticalDisplacement(-0.5f),
        m_capsuleHeight(1.7f),
        m_capsuleRadius(0.4f),
        m_maxSlopeForRotation(90.0f),
        m_collisionFilterInfo(0),
        m_phantomType(PHANTOM_TYPE_SIMPLE),
        m_linearVelocityMode(LINEAR_VELOCITY_MODE_WORLD),
        m_ignoreIncomingRotation(true),
        m_ignoreCollisionDuringRotation(true),
        m_ignoreIncomingTranslation(false),
        m_includeDownwardMomentum(true),
        m_followWorldFromModel(true),
        m_isTouchingGround(true),
        m_characterProxy(HK_NULL),
        m_phantom(HK_NULL),
        m_horizontalDisplacement(hkVector4::getZero()),
        m_verticalDisplacement(0.0f),
        m_timestep(0.0f),
        m_previousFrameFollowWorldFromModel(true)
    {
    }

    hkbProxyModifier::hkbProxyModifier(const hkbProxyModifier& mod)
    :   hkbModifier(mod),
        m_characterProxy(HK_NULL),
        m_phantom(HK_NULL),
        m_phantomShape(mod.m_phantomShape),
        m_horizontalDisplacement(hkVector4::getZero()),
        m_verticalDisplacement(0.0f),
        m_timestep(0.0f),
        m_previousFrameFollowWorldFromModel(true)
    {
        hkString::memCpy(&m_proxyInfo, &mod.m_proxyInfo, 0x97);
    }

    /// @note The capsule's height and radius are read before the up-vector calls (locals), as vanilla does.
    void hkbProxyModifier::activate(const hkbContext& context)
    {
        m_timestep                          = 0.0f;
        m_isTouchingGround                  = true;
        m_previousFrameFollowWorldFromModel = true;

        hkpWorld* world = context.getWorld();
        if (world) {
            hkpCharacterProxyCinfo cinfo;
            cinfo.m_position                      = context.m_character->m_worldFromModel->m_translation;
            cinfo.m_dynamicFriction               = m_proxyInfo.m_dynamicFriction;
            cinfo.m_staticFriction                = m_proxyInfo.m_staticFriction;
            cinfo.m_keepContactTolerance          = m_proxyInfo.m_keepContactTolerance;
            cinfo.m_up                            = m_proxyInfo.m_up;
            cinfo.m_keepDistance                  = m_proxyInfo.m_keepDistance;
            cinfo.m_contactAngleSensitivity       = m_proxyInfo.m_contactAngleSensitivity;
            cinfo.m_userPlanes                    = m_proxyInfo.m_userPlanes;
            cinfo.m_maxCharacterSpeedForSolver    = m_proxyInfo.m_maxCharacterSpeedForSolver;
            cinfo.m_characterStrength             = m_proxyInfo.m_characterStrength;
            cinfo.m_characterMass                 = m_proxyInfo.m_characterMass;
            cinfo.m_maxSlope                      = m_proxyInfo.m_maxSlope * HK_REAL_DEG_TO_RAD;
            cinfo.m_penetrationRecoverySpeed      = m_proxyInfo.m_penetrationRecoverySpeed;
            cinfo.m_maxCastIterations             = m_proxyInfo.m_maxCastIterations;
            cinfo.m_refreshManifoldInCheckSupport = m_proxyInfo.m_refreshManifoldInCheckSupport;

            hkpConvexShape* shape = m_phantomShape;
            if (!shape) {
                const hkReal height = m_capsuleHeight;
                const hkReal radius = m_capsuleRadius;
                hkVector4    vertexA;
                vertexA.setMul4(hkSimdReal(height - radius), context.getWorldUpWS());
                hkVector4 vertexB;
                vertexB.setMul4(hkSimdReal(radius), context.getWorldUpWS());
                shape = new hkpCapsuleShape(vertexA, vertexB, radius);
            }

            if (m_phantomType == PHANTOM_TYPE_SIMPLE) {
                m_phantom = new hkpSimpleShapePhantom(shape, hkTransform::getIdentity(), m_collisionFilterInfo);
            }
            else {
                m_phantom = new hkpCachingShapePhantom(shape, hkTransform::getIdentity(), m_collisionFilterInfo);
            }
            if (!m_phantomShape) shape->removeReference();

            world->lock();
            cinfo.m_shapePhantom = m_phantom;
            world->addPhantom(m_phantom);
            m_characterProxy = new hkpCharacterProxy(cinfo);
            world->unlock();
        }

        m_verticalDisplacement = 0.0f;
        m_horizontalDisplacement.setZero4();
    }

    void hkbProxyModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timestep += timestep;
    }

    void hkbProxyModifier::deactivate(const hkbContext& context)
    {
        hkpWorld* world = context.getWorld();
        if (world) {
            world->lock();
            world->removePhantom(m_phantom);
            if (m_characterProxy) {
                m_characterProxy->removeReference();
                m_characterProxy = HK_NULL;
            }
            world->unlock();
            if (m_phantom) {
                m_phantom->removeReference();
                m_phantom = HK_NULL;
            }
        }
    }

    hkbNode* hkbProxyModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbProxyModifier(*this);
    }

    /// @note Following the world-from-model again after a pause snaps the proxy to it (at rest) for that frame.
    void hkbProxyModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if (m_timestep > 0.0f) {
            context.getWorld()->lock();

            if (m_followWorldFromModel) {
                if (m_previousFrameFollowWorldFromModel) {
                    hkVector4 desiredVelocity;
                    computeDesiredVelocity(context, inOut.accessWorldFromModel(), desiredVelocity);
                    integrateProxy(context, desiredVelocity);
                    updateTranslation(context, inOut.accessWorldFromModel());
                    if (!m_ignoreIncomingRotation) updateRotation(context, inOut.accessWorldFromModel());
                }
                else {
                    m_characterProxy->setPosition(inOut.accessWorldFromModel().m_translation);
                    hkVector4 zero;
                    zero.setZero4();
                    m_characterProxy->setLinearVelocity(zero);
                }
            }
            m_previousFrameFollowWorldFromModel = m_followWorldFromModel;

            {
                m_timestep = 0.0f;
                hkpSurfaceInfo ground;
                hkVector4 down;
                down.setNeg4(context.getWorldUpWS());
                m_characterProxy->checkSupport(down, ground);
                context.getWorld()->unlock();

                m_isTouchingGround = (ground.m_supportedState == hkpSurfaceInfo::SUPPORTED);
            }
        }
    }

    void hkbProxyModifier::integrateProxy(const hkbContext& context, const hkVector4& desiredVelocity)
    {
        hkVector4 velocity;
        velocity.setAddMul4(desiredVelocity, m_horizontalDisplacement, hkSimdReal(m_horizontalGain / m_timestep));
        m_characterProxy->setLinearVelocity(velocity);

        hkVector4 expectedPosition;
        expectedPosition.setAddMul4(m_characterProxy->getPosition(), velocity, hkSimdReal(m_timestep));

        hkStepInfo stepInfo;
        stepInfo.m_deltaTime    = m_timestep;
        stepInfo.m_invDeltaTime = 1.0f / m_timestep;
        m_characterProxy->integrate(stepInfo, context.getWorld()->getGravity());

        const hkVector4& up       = context.getWorldUpWS();
        const hkVector4& position = m_characterProxy->getPosition();
        hkVector4        horizontalPosition;
        horizontalPosition.setAddMul4(position, up, -up.dot3(position));
        hkVector4 expectedHorizontalPosition;
        expectedHorizontalPosition.setAddMul4(expectedPosition, up, -up.dot3(expectedPosition));
        m_horizontalDisplacement.setSub4(expectedHorizontalPosition, horizontalPosition);

        const hkReal maxSeparationSquared = m_maxHorizontalSeparation * m_maxHorizontalSeparation;
        const hkReal separationSquared    = m_horizontalDisplacement.lengthSquared3().getReal();
        if (separationSquared > maxSeparationSquared) {
            m_horizontalDisplacement.mul4(hkSimdReal(::sqrtf(maxSeparationSquared / separationSquared)));
        }
    }

    void hkbProxyModifier::updateTranslation(const hkbContext& context, hkQsTransform& worldFromModel)
    {
        m_verticalDisplacement += m_verticalDisplacementErrorGain * m_verticalDisplacementError;
        m_verticalDisplacement = (m_verticalDisplacement < m_minVerticalDisplacement) ? m_minVerticalDisplacement : hkMath::min2(m_maxVerticalDisplacement, m_verticalDisplacement);

        const hkVector4& up = context.getWorldUpWS();
        hkVector4        target;
        target.setAddMul4(m_characterProxy->getPosition(), up, hkSimdReal(m_verticalDisplacement));
        target.add4(m_horizontalDisplacement);

        const hkReal targetHeight  = up.dot3(target).getReal();
        const hkReal currentHeight = up.dot3(worldFromModel.m_translation).getReal();
        hkReal       height        = (targetHeight - currentHeight) * m_verticalGain + currentHeight;
        const hkReal separation    = height - targetHeight;
        if (separation > m_maxVerticalSeparation) height = targetHeight + m_maxVerticalSeparation;
        if (separation < -m_maxVerticalSeparation) height = targetHeight - m_maxVerticalSeparation;

        hkVector4 translation;
        translation.setAddMul4(target, up, -up.dot3(target));
        translation.addMul4(up, hkSimdReal(height));
        worldFromModel.m_translation = translation;
    }

    void hkbProxyModifier::updateRotation(const hkbContext& context, hkQsTransform& worldFromModel)
    {
        const hkTransform oldTransform = m_phantom->getTransform();
        hkTransform       newTransform;
        newTransform.m_translation = oldTransform.m_translation;
        newTransform.m_rotation.set(worldFromModel.m_rotation);
        newTransform.m_rotation.renormalize();
        m_phantom->setTransform(newTransform);

        if (!m_ignoreCollisionDuringRotation) {
            hkpAllCdPointCollector collector;
            context.getWorld()->getClosestPoints(m_phantom->getCollidable(), *context.getWorld()->getCollisionInput(), collector);

            const hkArray<hkRootCdPoint>& hits = collector.getHits();
            if (hits.getSize() > 0) {
                for (int i = 0; i < hits.getSize(); i++) {
                    const hkReal cosAngle = context.getWorldUpWS().dot3(hits[i].m_contact.m_separatingNormal).getReal();
                    hkReal       angle;
                    if (hkMath::fabs(cosAngle) >= 1.0f) angle = (cosAngle > 0.0f) ? 0.0f : HK_REAL_PI;
                    else                                angle = ::acosf(cosAngle);

                    if (angle > m_maxSlopeForRotation * HK_REAL_DEG_TO_RAD) {
                        m_phantom->setTransform(oldTransform);
                        worldFromModel.m_rotation.set(oldTransform.m_rotation);
                        break;
                    }
                }
            }
        }
    }
} }
