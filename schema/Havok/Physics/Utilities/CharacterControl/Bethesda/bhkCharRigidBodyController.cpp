/// @file
/// @brief bhkCharRigidBodyController (and its cinfo), reimplemented against vanilla (AE 1.6.1170).
/// @note Bethesda's engine code: built with C++ exceptions on and /Ob2 (the destructor is inlined into the deleting
///       one), which the manifest sets on this unit.
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharRigidBodyController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharRigidBodyControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/Bethesda/Setting.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

namespace CB { namespace schema
{
    bhkCharRigidBodyController::bhkCharRigidBodyController(bhkCharRigidBodyControllerCinfo& cinfo)
    :   bhkCharacterController(cinfo),
        m_ignoredCollisionStartCollector(HK_NULL)
    {
        Init(cinfo);
    }

    void bhkCharRigidBodyController::Init(bhkCharRigidBodyControllerCinfo& cinfo)
    {
        InitFromCinfo(cinfo);
        bhkShape* shape = m_shapeIndex < 2 ? NiPointer<bhkShape>(m_shapes[m_shapeIndex]) : NiPointer<bhkShape>();
        cinfo.m_rigidBodyCinfo.m_collisionFilterInfo = cinfo.m_collisionFilterInfo;
        cinfo.m_rigidBodyCinfo.m_shape               = shape->GetShape();
        cinfo.m_rigidBodyCinfo.m_up                  = m_up;
        cinfo.m_rigidBodyCinfo.m_maxLinearVelocity   = 50.0f;
        m_characterRigidBody.CreateFromCinfo(&static_cast<hkSerializableCinfo&>(cinfo.m_rigidBodyCinfo));
        m_rigidBody = new bhkRigidBody();
        m_rigidBody->SetReferencedObject(m_characterRigidBody.GetRigidBody());
        m_characterRigidBody.SetListener(this);
        if (cinfo.m_world) SetWorld(cinfo.m_world);
        AddHavokProperty(1002, reinterpret_cast<hkUlong>(this));
    }

    bhkCharRigidBodyController::~bhkCharRigidBodyController()
    {
        if (IsInWorld()) m_characterRigidBody.RemoveListener();
    }

    void bhkCharRigidBodyController::GetPositionImpl(hkVector4& pos, bool applyCenterOffset) const
    {
        if (!m_characterRigidBody.GetRigidBody()) {
            pos = hkVector4::getZero();
            return;
        }
        hkVector4 offset = hkVector4::getZero();
        bhkWorldLock lock(GetHavokWorld(), 0);
        hkTransform transform;
        if (bUseCharRBExtrapolation_HAVOK.m_value.b && m_characterRigidBody.GetRigidBody()->getWorld() &&
            !m_characterRigidBody.GetRigidBody()->getWorld()->isSimulationAtPsi()) {
            hkpRigidBody* body = m_characterRigidBody.GetRigidBody();
            const hkTime time = body->getWorld()->getCurrentTime();
            body->approxTransformAt(time, transform);
        } else {
            transform = m_characterRigidBody.GetRigidBody()->getTransform();
        }
        if (applyCenterOffset) offset.setRotatedDir(transform.m_rotation, m_centerOffset);
        pos.setSub4(transform.m_translation, offset);
    }

    void bhkCharRigidBodyController::SetPositionImpl(const hkVector4& pos, bool applyCenterOffset, bool moveByVelocity)
    {
        bhkWorldLock lock(m_characterRigidBody.GetRigidBody()->getWorld(), 1);
        hkVector4 offset = hkVector4::getZero();
        if (applyCenterOffset) offset.setRotatedDir(m_characterRigidBody.GetRigidBody()->getTransform().m_rotation, m_centerOffset);
        hkVector4 newPos;
        newPos.setAdd4(pos, offset);
        hkVector4 velocity = hkVector4::getZero();
        if (!newPos.equals3(m_characterRigidBody.GetRigidBody()->getPosition(), 1e-6f)) {
            const hkReal deltaTime = bhkWorld::s_stepDeltaTime;
            bhkWorld* const world = GetHavokWorld();
            if (moveByVelocity && deltaTime > 0.0f && world && !IsFlagSet(17)) {
                hkVector4 centerOfMass;
                centerOfMass.setRotatedDir(m_characterRigidBody.GetRigidBody()->getRotation(), m_characterRigidBody.GetRigidBody()->getCenterOfMassLocal());
                centerOfMass.add4(newPos);
                velocity.setSub4(centerOfMass, m_characterRigidBody.GetRigidBody()->getCenterOfMassInWorld());
                velocity.mul4(1.0f / deltaTime);
            } else {
                m_characterRigidBody.SetPosition(newPos);
            }
        }
        hkpCharacterRigidBody* characterRB = m_characterRigidBody.GetCharacterRigidBody();
        if (characterRB) characterRB->setLinearVelocity(velocity, m_stepInfo.m_deltaTime);
    }

    void bhkCharRigidBodyController::GetTransformImpl(hkTransform& transform) const
    {
        transform.m_translation = m_characterRigidBody.GetRigidBody()->getTransform().m_translation;
        transform.m_rotation    = m_characterRigidBody.GetRigidBody()->getTransform().m_rotation;
    }

    void bhkCharRigidBodyController::SetTransformImpl(const hkTransform& transform)
    {
        bhkWorldLock lock(GetHavokWorld(), 1);
        m_characterRigidBody.GetRigidBody()->setTransform(transform);
    }

    void bhkCharRigidBodyController::GetLinearVelocityImpl(hkVector4& velocity) const
    {
        hkpCharacterRigidBody* characterRB = m_characterRigidBody.GetCharacterRigidBody();
        if (characterRB) velocity = characterRB->getLinearVelocity();
        else             velocity = hkVector4::getZero();
    }

    void bhkCharRigidBodyController::SetLinearVelocityImpl(const hkVector4& velocity)
    {
        bhkWorldLock lock(m_characterRigidBody.GetRigidBody()->getWorld(), 1);
        if (m_characterRigidBody.GetCharacterRigidBody()) m_characterRigidBody.GetCharacterRigidBody()->setLinearVelocity(velocity, m_stepInfo.m_deltaTime);
    }

    CFilter bhkCharRigidBodyController::GetCollisionFilterInfo() const
    {
        bhkWorldLock lock(m_characterRigidBody.GetRigidBody()->getWorld(), 0);
        CFilter filter;
        filter.m_filter = m_characterRigidBody.GetRigidBody()->getCollisionFilterInfo();
        return filter;
    }

    void bhkCharRigidBodyController::SetCollisionFilterInfo(CFilter filter)
    {
        bhkWorldLock lock(m_characterRigidBody.GetRigidBody()->getWorld(), 1);
        m_characterRigidBody.GetRigidBody()->getCollidableRw()->setCollisionFilterInfo(filter.m_filter);
    }

    const hkVector4 bhkCharRigidBodyController::s_horizontalSum(1.0f, 1.0f, 0.0f, 0.0f);

    bool bhkCharRigidBodyController::Unk_0B()
    {
        bhkWorldLock lock(GetHavokWorld(), 1);
        m_characterRigidBody.GetCharacterRigidBody()->getRigidBody()->setFriction((m_flags & 8) ? 0.0f : 1.0f);
        hkVector4 velocity;
        GetLinearVelocityImpl(velocity);
        float horizontal;
        if (!IsFlagSet(8) ||
            (horizontal = m_outVelocity.dot3(s_horizontalSum).getReal(), horizontal != 0.0f) ||
            (horizontal = velocity.dot3(s_horizontalSum).getReal(), horizontal != 0.0f)) {
            SetLinearVelocityImpl(m_outVelocity);
        }
        if (m_characterRigidBody.GetCharacterRigidBody()->getRigidBody()->isActive()) ResetContactState();
        ProcessCharacterContacts();
        return false;
    }

    bool bhkCharRigidBodyController::Unk_0A()
    {
        bool counted = false;
        BSScrapArray<hkpCollidable*> bodies(20);
        GetContactCollidables(bodies);
        for (hkUint32 i = 0; i < bodies.GetSize() && !counted; ++i) {
            counted = IsLayerCounted(bodies[i]->getCollisionFilterInfo() & 0x7f);
        }
        return counted;
    }

    void bhkCharRigidBodyController::Unk_0C() {}

    void bhkCharRigidBodyController::CheckSupportImpl()
    {
        m_characterRigidBody.CheckSupport(m_stepInfo, m_surfaceInfo);
        m_supportNorm = m_surfaceInfo.m_surfaceNormal;
    }

    void bhkCharRigidBodyController::SetWorld(bhkWorld* world)
    {
        bhkWorld* current = GetHavokWorld();
        if (current == world) return;
        if (current) {
            m_rigidBody->RemoveFromCurrentWorld();
            m_ignoredCollisionStartCollector.reset();
        }
        if (world) {
            m_rigidBody->MoveToWorld(world);
            hkpCharacterRigidBody* characterRB = m_characterRigidBody.GetCharacterRigidBody();
            bhkSetDebugDisplayColor(characterRB ? characterRB->getRigidBody() : HK_NULL, hkColor::rgbFromFloats(1.0f, 1.0f, 0.0f, 0.75f));
        }
    }

    bhkWorld* bhkCharRigidBodyController::GetHavokWorld() const
    {
        ahkpWorld* world = const_cast<bhkCharacterRigidBody&>(m_characterRigidBody).GetWorld2();
        return world ? world->m_bhkWorld : HK_NULL;
    }

    hkpRigidBody* bhkCharRigidBodyController::GetRigidBody() const
    {
        return m_characterRigidBody.GetRigidBody();
    }

    float bhkCharRigidBodyController::GetVDBAlpha() const { return 0.75f; }

    void bhkCharRigidBodyController::RotateImpl(const hkTransform& transform)
    {
        const hkReal deltaTime = bhkWorld::s_stepDeltaTime;
        bhkWorld* const world = GetHavokWorld();
        if (deltaTime > 0.0f && world && !IsFlagSet(17)) {
            hkTransform current;
            GetTransformImpl(current);
            hkQuaternion currentRotation;
            currentRotation.set(current.m_rotation);
            hkQuaternion targetRotation;
            targetRotation.set(transform.m_rotation);
            hkQuaternion inverse;
            inverse.setInverse(currentRotation);
            hkQuaternion delta;
            delta.setMul(targetRotation, inverse);
            delta.normalize();
            hkVector4 angularVelocity = hkVector4::getZero();
            const hkReal angle = delta.getAngle();
            if (angle > 0.001f) {
                hkVector4 axis;
                delta.getAxis(axis);
                angularVelocity.setMul4(hkSimdReal(angle / deltaTime), axis);
            }
            const float speedSquared = angularVelocity.lengthSquared3().getReal();
            const hkReal maxSpeed = m_characterRigidBody.GetRigidBody()->getMaxAngularVelocity();
            if (speedSquared <= maxSpeed * maxSpeed) {
                bhkWorldLock lock(world, 1);
                m_characterRigidBody.GetCharacterRigidBody()->setAngularVelocity(angularVelocity);
                return;
            }
        }
        SetTransformImpl(transform);
    }

    bhkCharRigidBodyControllerCinfo::bhkCharRigidBodyControllerCinfo() : bhkCharacterControllerCinfo(true, true) {}

    void bhkCharRigidBodyControllerCinfo::SetPosition(const hkVector4& position) { m_rigidBodyCinfo.m_position = position; }

    float bhkCharRigidBodyControllerCinfo::Unk_02() const { return 0.0f; }

    float bhkCharRigidBodyControllerCinfo::Unk_03() const { return 0.0f; }

    void bhkCharRigidBodyControllerCinfo::Unk_04(float) {}

    float bhkCharRigidBodyControllerCinfo::Unk_05() const { return 1.0f; }


    void bhkCharRigidBodyController::ProcessCharacterContacts()
    {
        m_numCharacterContacts = 0;
        BSScrapArray<hkpCollidable*> bodies(20);
        GetContactCollidables(bodies);
        for (hkUint32 i = 0; i < bodies.GetSize(); ++i) {
            const hkUint32 filterInfo = bodies[i]->getCollisionFilterInfo();
            if ((filterInfo & 0x7f) == 30 && (!m_systemGroup || (filterInfo >> 16) == m_systemGroup)) {
                ++m_numCharacterContacts;
                if (m_stopOnCharacterContact) {
                    hkVector4 velocity;
                    velocity(0) = NiPoint3::ZERO.x * 0.0142875f;
                    velocity(1) = NiPoint3::ZERO.y * 0.0142875f;
                    velocity(2) = NiPoint3::ZERO.z * 0.0142875f;
                    velocity(3) = 0.0f;
                    SetLinearVelocityImpl(velocity);
                }
            }
        }
    }
} }
