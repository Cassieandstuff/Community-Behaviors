/// @file
/// @brief bhkCharProxyController (and its cinfo), being matched against vanilla (AE 1.6.1170).
/// @note Bethesda's engine code: built with C++ exceptions on and /Ob2, as the rigid-body controller's unit.
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterCollisionHandler.h>
#include <Physics/Collide/Query/Collector/BodyPairCollector/hkpAllCdBodyPairCollector.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    namespace
    {
        /// @brief The Bethesda wrapper of the proxy's shape phantom, null without a proxy or phantom.
        HK_FORCE_INLINE bhkWorldObject* GetProxyPhantomWrapper(const bhkCharacterProxy& characterProxy)
        {
            hkpCharacterProxy* proxy   = characterProxy.GetCharacterProxy();
            hkpShapePhantom*   phantom = proxy ? proxy->getShapePhantom() : HK_NULL;
            return phantom ? reinterpret_cast<bhkWorldObject*>(phantom->getUserData()) : HK_NULL;
        }
    }

    bhkCharProxyControllerCinfo::bhkCharProxyControllerCinfo() : bhkCharacterControllerCinfo(true, false)
    {
        m_proxyCinfo.m_userPlanes                 = 4;
        m_proxyCinfo.m_maxCharacterSpeedForSolver = 10.0f;
        m_proxyCinfo.m_position                   = hkVector4::getZero();
        m_proxyCinfo.m_velocity                   = hkVector4::getZero();
        m_proxyCinfo.m_shapePhantom               = HK_NULL;
        m_proxyCinfo.m_dynamicFriction            = 1.0f;
        m_proxyCinfo.m_staticFriction             = 0.0f;
        m_proxyCinfo.m_extraUpStaticFriction      = 0.0f;
        m_proxyCinfo.m_extraDownStaticFriction    = 0.0f;
    }

    bhkCharProxyControllerCinfo::~bhkCharProxyControllerCinfo() {}

    float bhkCharProxyControllerCinfo::Unk_02() const { return m_proxyCinfo.m_keepDistance; }

    float bhkCharProxyControllerCinfo::Unk_03() const { return m_proxyCinfo.m_contactAngleSensitivity; }

    void bhkCharProxyControllerCinfo::Unk_04(float value) { m_proxyCinfo.m_contactAngleSensitivity = value; }

    void bhkCharProxyControllerCinfo::SetPosition(const hkVector4& position) { m_proxyCinfo.m_position = position; }

    float bhkCharProxyControllerCinfo::Unk_05() const { return 1.0f; }

    void bhkCharProxyController::contactPointAddedCallback(const hkpCharacterProxy*, const hkRootCdPoint& point)
    {
        const hkUint32 filterInfo = point.m_rootCollidableB->getCollisionFilterInfo();
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

    void bhkCharProxyController::contactPointRemovedCallback(const hkpCharacterProxy*, const hkRootCdPoint& point)
    {
        if (m_numCharacterContacts) {
            const hkUint32 filterInfo = point.m_rootCollidableB->getCollisionFilterInfo();
            if ((filterInfo & 0x7f) == 30 && (!m_systemGroup || (filterInfo >> 16) == m_systemGroup)) --m_numCharacterContacts;
        }
    }

    void bhkCharProxyController::objectInteractionCallback(hkpCharacterProxy*, const hkpCharacterObjectInteractionEvent& input,
                                                           hkpCharacterObjectInteractionResult& output)
    {
        HandleObjectInteraction(input, output);
    }

    void bhkCharProxyController::characterInteractionCallback(hkpCharacterProxy*, hkpCharacterProxy* otherProxy, const hkContactPoint& contact)
    {
        bhkCharacterController* other = reinterpret_cast<bhkCharacterController*>(GetHavokProperty(1002, otherProxy->getShapePhantom()));
        PushCharacter(other, contact);
        bhkCharacterController& self = *this;
        if (bhkCharacterCollisionHandler::s_singleton) bhkCharacterCollisionHandler::s_singleton->HandleCharacterCollision(&self, other);
    }

    hkpRigidBody* bhkCharProxyController::GetRigidBody() const
    {
        return m_shapePhantom ? reinterpret_cast<hkpRigidBody*>(m_shapePhantom->m_referencedObject) : HK_NULL;
    }

    void bhkCharProxyController::SetWorld(bhkWorld* world)
    {
        hkpCharacterProxy* proxy   = m_characterProxy.GetCharacterProxy();
        bhkWorld*          current = GetHavokWorld();
        if (current == world) return;
        if (current) {
            m_characterProxy.ReleaseContacts();
            m_shapePhantom->RemoveFromCurrentWorld();
            proxy->getManifold().clear();
        }
        if (world) m_shapePhantom->MoveToWorld(world);
    }

    bhkWorld* bhkCharProxyController::GetHavokWorld() const
    {
        ahkpWorld* world = const_cast<bhkCharacterProxy&>(m_characterProxy).GetWorld2();
        return world ? world->m_bhkWorld : HK_NULL;
    }

    float bhkCharProxyController::GetVDBAlpha() const { return 0.75f; }

    void bhkCharProxyController::SetPositionImpl(const hkVector4& pos, bool applyCenterOffset, bool)
    {
        hkVector4 offset = hkVector4::getZero();
        if (applyCenterOffset) offset.setRotatedDir(m_shapePhantom->GetPhantom()->getTransform().m_rotation, m_centerOffset);
        hkVector4 newPos;
        newPos.setAdd4(pos, offset);
        m_characterProxy.SetPosition(newPos);
    }

    void bhkCharProxyController::GetPositionImpl(hkVector4& pos, bool applyCenterOffset) const
    {
        hkVector4 offset = hkVector4::getZero();
        if (applyCenterOffset) offset.setRotatedDir(m_shapePhantom->GetPhantom()->getTransform().m_rotation, m_centerOffset);
        pos.setSub4(const_cast<bhkCharacterProxy&>(m_characterProxy).GetPosition(), offset);
    }

    void bhkCharProxyController::GetTransformImpl(hkTransform& transform) const
    {
        transform.m_translation = m_shapePhantom->GetPhantom()->getTransform().m_translation;
        transform.m_rotation    = m_shapePhantom->GetPhantom()->getTransform().m_rotation;
    }

    void bhkCharProxyController::SetTransformImpl(const hkTransform& transform)
    {
        bhkWorldLock lock(m_shapePhantom->GetWorld2(), 1);
        m_shapePhantom->GetPhantom()->setTransform(transform);
    }

    void bhkCharProxyController::GetLinearVelocityImpl(hkVector4& velocity) const
    {
        hkpCharacterProxy* proxy = m_characterProxy.GetCharacterProxy();
        if (proxy) velocity = proxy->getLinearVelocity();
        else       velocity = hkVector4::getZero();
    }

    void bhkCharProxyController::SetLinearVelocityImpl(const hkVector4& velocity)
    {
        hkpCharacterProxy* proxy = m_characterProxy.GetCharacterProxy();
        if (proxy) proxy->setLinearVelocity(velocity);
    }

    CFilter bhkCharProxyController::GetCollisionFilterInfo() const
    {
        if (!GetProxyPhantomWrapper(m_characterProxy)) {
            CFilter filter;
            filter.m_filter = 0;
            return filter;
        }
        const hkpWorldObject* object     = static_cast<const hkpWorldObject*>(GetProxyPhantomWrapper(m_characterProxy)->m_referencedObject);
        const hkpCollidable*  collidable = object ? object->getCollidable() : HK_NULL;
        CFilter filter;
        filter.m_filter = 0;
        if (object && collidable) filter.m_filter = collidable->getCollisionFilterInfo();
        return filter;
    }

    void bhkCharProxyController::SetCollisionFilterInfo(CFilter filter)
    {
        if (GetProxyPhantomWrapper(m_characterProxy)) {
            bhkWorldObject* object     = GetProxyPhantomWrapper(m_characterProxy);
            hkpCollidable*  collidable = object->GetCollidableRw();
            if (collidable) collidable->setCollisionFilterInfo(filter.m_filter);
            object->UpdateCollisionFilter();
        }
    }

    bool bhkCharProxyController::Unk_0B()
    {
        SetLinearVelocityImpl(m_outVelocity);
        m_highSpeed = m_outVelocity.lengthSquared3().getReal() > 500.0f;
        hkpCharacterProxy* proxy = m_characterProxy.GetCharacterProxy();
        proxy->m_maxCastIterations = IsFlagSet(20) ? 1 : 10;
        m_characterProxy.Integrate(m_stepInfo);
        proxy->m_maxCastIterations = 10;
        return true;
    }

    void bhkCharProxyController::Unk_0C()
    {
        hkVector4 position;
        GetPositionImpl(position, true);
        const hkQuadReal q = position.m_quad;
        bhkCharacterMoveFinishEvent event;
        event.m_position.x = q.m128_f32[0] * 69.99125f;
        event.m_position.y = q.m128_f32[1] * 69.99125f;
        event.m_position.z = q.m128_f32[2] * 69.99125f;
        SendEvent(&event);
    }

    void bhkCharProxyController::CheckSupportImpl()
    {
        m_flags &= ~0x400u;
        m_characterProxy.CheckSupport(s_down, m_surfaceInfo);
    }

    bool bhkCharProxyController::GetContactCollidables(BSScrapArray<hkpCollidable*>& collidables)
    {
        hkpCachingShapePhantom* phantom = m_shapePhantom->GetPhantom();
        phantom->ensureDeterministicOrder();
        for (int i = 0; i < phantom->m_collisionDetails.getSize(); i++) {
            hkpCollidable* collidable = phantom->m_collisionDetails[i].m_collidable;
            collidables.PushBack(collidable);
        }
        return true;
    }

    bool bhkCharProxyController::Unk_0A()
    {
        bool               counted = false;
        hkpCharacterProxy* proxy   = m_characterProxy.GetCharacterProxy();
        hkpShapePhantom*   phantom = proxy ? proxy->getShapePhantom() : HK_NULL;
        hkpWorld*          world   = phantom ? phantom->getWorld() : HK_NULL;
        if (world) {
            hkpAllCdBodyPairCollector collector;
            {
                bhkWorldLock lock(world, 1);
                phantom->getPenetrations(collector);
            }
            for (int i = 0; i < collector.getHits().getSize() && !counted; ++i) {
                counted = IsLayerCounted(collector.getHits()[i].m_rootCollidableB->getCollisionFilterInfo() & 0x7f);
            }
        }
        return counted;
    }

    void bhkCharProxyController::RotateImpl(const hkTransform& transform)
    {
        SetTransformImpl(transform);
    }
} }
