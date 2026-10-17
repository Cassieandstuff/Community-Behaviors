/// @file
/// @brief Workbench: the functions of hkbProxyModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Behavior/Modifier/Proxy/hkbProxyModifier.cpp.
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
    void hkbProxyModifier::computeDesiredVelocity(const hkbContext& context, const hkQsTransform& worldFromModel, hkVector4& desiredVelocityOut)
    {
        if (m_ignoreIncomingTranslation) {
            desiredVelocityOut = m_characterProxy->getLinearVelocity();
        }
        else {
            hkQsTransform motion;
            motion.setMulInverseMul(*context.m_character->m_worldFromModel, worldFromModel);
            hkVector4 velocity;
            velocity.setRotatedDir(worldFromModel.m_rotation, motion.m_translation);
            desiredVelocityOut.setMul4(hkSimdReal(1.0f / m_timestep), velocity);
        }

        hkVector4 linearVelocity;
        if (m_linearVelocityMode == LINEAR_VELOCITY_MODE_WORLD) {
            linearVelocity = m_linearVelocity;
        }
        else {
            linearVelocity.setRotatedDir(worldFromModel.m_rotation, m_linearVelocity);
        }
        desiredVelocityOut.add4(linearVelocity);

        if (m_includeDownwardMomentum && !m_ignoreIncomingTranslation) {
            const hkVector4& up            = context.getWorldUpWS();
            hkReal           verticalSpeed = up.dot3(m_characterProxy->getLinearVelocity()).getReal();
            if (verticalSpeed >= 0.0f) verticalSpeed = 0.0f;
            desiredVelocityOut.addMul4(hkSimdReal(verticalSpeed), up);
        }

        desiredVelocityOut.addMul4(context.getWorld()->getGravity(), hkSimdReal(m_timestep));
    }

} }
