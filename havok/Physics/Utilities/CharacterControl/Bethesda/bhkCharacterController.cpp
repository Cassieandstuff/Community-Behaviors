/// @file
/// @brief bhkCharacterController (its destructor, state-machine helpers and contact helpers) and
///        bhkCharacterControllerCinfo (its destructor), reimplemented against vanilla (AE 1.6.1170).
/// @note Bethesda's engine code: built with C++ exceptions on and /Ob2 (each destructor is inlined into its deleting
///       one), which the manifest sets on this unit.
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>

namespace CB { namespace havok
{
    bhkCharacterControllerCinfo::~bhkCharacterControllerCinfo()
    {
        m_stateManager = HK_NULL;
        for (int i = 0; i < 2; ++i) m_shapes[i] = HK_NULL;
    }

    bhkCharacterController::~bhkCharacterController()
    {
        MemoryContextScope context(0x36);
        ClearDamageImpacts();
        m_bumpedBody                = HK_NULL;
        m_bumpedCharCollisionObject = HK_NULL;
        m_supportBody               = HK_NULL;
        if (m_orientationCtrl->IsOwnedByController()) delete m_orientationCtrl;
    }

    void bhkCharacterController::ResetContactState()
    {
        m_flags &= ~4u;
        m_supportNorm.setZero4();
        m_orientationCtrl->Reset();
    }

    void bhkCharacterController::CommitWantedState()
    {
        if (m_wantState != HK_CHARACTER_MAX_STATE_ID) {
            m_context.m_currentState = m_wantState;
            m_wantState              = HK_CHARACTER_MAX_STATE_ID;
        }
    }

    void bhkCharacterController::AccumulateFallTime()
    {
        m_fallTime += m_stepInfo.m_deltaTime;
    }

    void bhkCharacterController::ApplyInitialVelocity()
    {
        if (m_velocityTime > 0.0f) {
            if (!(m_flags & 0x1800)) m_outVelocity.addMul4(hkSimdReal(m_velocityTime), m_initialVelocity);
        } else {
            m_initialVelocity = hkVector4::getZero();
        }
    }

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

    void bhkCharacterController::SetSupportBody(hkpRigidBody* body)
    {
        m_supportBody = body;
    }

    bhkCharacterController* bhkCharacterController::GetControllerFromCollidable(const hkpWorldObject* object)
    {
        return reinterpret_cast<bhkCharacterController*>(GetHavokProperty(1002, object));
    }

    /// @note As vanilla: `other`'s velocity is read into the local this controller's own then overwrites (only this
    ///       one's speed pushes), and the impulse is scaled to game units and back (s_worldScaleInverse, then 0.0142875).
    void bhkCharacterController::PushCharacter(bhkCharacterController* other, const hkContactPoint& contact)
    {
        m_bumpedCharCollisionObject = other->GetRigidBody();
        if (other->m_pushPriority <= m_pushPriority && !(other->m_flags & 0x4000)) {
            hkVector4 velocity;
            other->GetLinearVelocityImpl(velocity);
            hkVector4 direction = contact.m_separatingNormal;
            direction(2) = 0.0f;
            const hkSimdReal lengthSquared = direction.lengthSquared3();
            if (lengthSquared.getReal() > 0.0f) direction.mul4(hkMath::sqrtInverseOfSplat(lengthSquared));
            GetLinearVelocityImpl(velocity);
            velocity(2) = 0.0f;
            hkReal speed = velocity.length3().getReal();
            if (m_unk2a0 == 2) speed += 20.0f;
            speed *= -0.25f;
            hkVector4 push;
            push.setMul4(hkSimdReal(speed), direction);
            other->m_pushDelta.add4(push);
            ++other->m_pushCount;
            hkVector4 impulse;
            impulse(0) = push(0) * bhkWorld::s_worldScaleInverse * 0.0142875f;
            impulse(1) = push(1) * bhkWorld::s_worldScaleInverse * 0.0142875f;
            impulse(2) = push(2) * bhkWorld::s_worldScaleInverse * 0.0142875f;
            impulse(3) = 0.0f;
            impulse.mul4(hkSimdReal(100.0f));
            if (impulse.lengthSquared3().getReal() > other->m_initialVelocity.lengthSquared3().getReal()) {
                other->m_velocityTime    = 0.01f;
                other->m_initialVelocity = impulse;
            }
            m_orientationCtrl->OnPushCharacter(this, contact, other);
        }
    }

    void bhkCharacterController::UpdateContactMaterial(const hkRootCdPoint& point)
    {
        if (m_surfaceMaterial == 0) {
            const hkpCollidable* collidable = point.m_rootCollidableB;
            hkUint32 material = 0;
            if (collidable) {
                const hkUint32 layer = static_cast<const hkpCollidable*>(collidable->getRootCdBody())->getCollisionFilterInfo() & 0x7f;
                m_contactLayer = layer;
                const hkpRigidBody* body = hkpGetRigidBody(static_cast<const hkpCollidable*>(collidable->getRootCdBody()));
                m_contactVelocity = body ? body->getLinearVelocity() : hkVector4::getZero();
                if (point.m_shapeKeyB == HK_INVALID_SHAPE_KEY) {
                    const hkpShape* shape = collidable->getShape();
                    if (shape) {
                        const bhkShape* wrapper = reinterpret_cast<const bhkShape*>(shape->getUserData());
                        if (wrapper) material = wrapper->m_materialID;
                    }
                } else if (layer != 17) {
                    const bhkShapeCollection* collection = bhkGetShapeCollection(collidable);
                    if (collection) {
                        if (collection->GetShape()->getType() == HK_SHAPE_LIST && static_cast<const bhkListShape*>(collection)->m_useListMaterial) {
                            material = collection->m_materialID;
                        } else {
                            material = collection->GetChildMaterialID(point.m_shapeKeyB);
                        }
                    }
                }
                m_surfaceMaterial = material;
                if (bhkIsStairsMaterial(material)) m_flags |= 0x1000000;
                else                               m_flags &= ~0x1000000u;
            }
        }
    }
} }
