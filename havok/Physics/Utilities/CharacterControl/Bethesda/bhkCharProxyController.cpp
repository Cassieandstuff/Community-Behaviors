/// @file
/// @brief bhkCharProxyController (and its cinfo), reimplemented against vanilla (AE 1.6.1170); vanilla's unit
///        (0x140ed0540..0x140ed2d50) also holds the bhkCharacterProxy and hkpCharacterProxyListener copies its
///        vtables pull in.
/// @note Bethesda's engine code: built with C++ exceptions on and /Ob2, as the rigid-body controller's unit, which
///       the manifest sets on this unit.
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterCollisionHandler.h>
#include <Physics/Collide/Query/Collector/BodyPairCollector/hkpAllCdBodyPairCollector.h>
#include <Physics/Dynamics/World/hkpWorld.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCollisionFilter.h>
#include <Physics/Utilities/CharacterControl/Bethesda/Setting.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>
#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxy.h>
#include <Physics/ConstraintSolver/Simplex/hkpSimplexSolver.h>
#include <Physics/Collide/Shape/Compound/Collection/List/hkpListShape.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Physics/Dynamics/Phantom/hkpShapePhantom.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

namespace CB { namespace havok
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

    /// @note Not matched yet (workbench.yaml tracks it): a driver here, its inlined members emitting the vtables the
    ///       listed bhkCharacterProxy / hkpCharacterProxyListener copies and deleting destructors come out of.
    bhkCharProxyController::bhkCharProxyController(bhkCharProxyControllerCinfo& cinfo)
    :   bhkCharacterController(cinfo),
        m_highSpeed(false)
    {
        Init(cinfo);
        BSSpinLockGuard guard(s_controllersLock);
        s_controllers.PushBack(this);
    }

    /// @note Not matched yet (workbench.yaml tracks it): a driver here, its local cinfo emitting the listed
    ///       ~bhkShapePhantomCinfo.
    void bhkCharProxyController::Init(bhkCharProxyControllerCinfo& cinfo)
    {
        InitFromCinfo(cinfo);
        bhkShape* shape = m_shapeIndex < 2 ? NiPointer<bhkShape>(m_shapes[m_shapeIndex]) : NiPointer<bhkShape>();
        bhkShapePhantomCinfo phantomCinfo;
        phantomCinfo.m_collisionFilterInfo = cinfo.m_collisionFilterInfo;
        phantomCinfo.m_shape               = shape->GetShape();
        m_shapePhantom = new bhkCachingShapePhantom(phantomCinfo);
        if (cinfo.m_world) m_shapePhantom->MoveToWorld(cinfo.m_world);
        cinfo.m_proxyCinfo.m_shapePhantom = m_shapePhantom->GetPhantom();
        hkSerializableCinfo& proxyCinfo = cinfo.m_proxyCinfo;
        m_characterProxy.CreateFromCinfo(&proxyCinfo);
        m_characterProxy.AddListener(this);
        AddHavokProperty(1002, reinterpret_cast<hkUlong>(static_cast<bhkCharacterController*>(this)));
    }

    /// @note Matches as VARIANT with no --diff-gated rule; --diff agrees on every seed it runs to the end (t_memoryContext
    ///       placed at the game's TLS block offset). Spellings it depends on: kTryStep cleared through the controller base
    ///       pointer (vanilla's read-modify-write in memory, the flags reloaded for the kCheckSupport test); the capsule
    ///       tops in sibling scopes (vanilla's frame shares their slot with the loop's plane); m_previousState read
    ///       before hkpCharacterContext::getState; the phantom layers 14 / 30 as a switch.
    void bhkCharProxyController::processConstraintsCallback(const hkpCharacterProxy* proxy, const hkArray<hkRootCdPoint>& manifold,
                                                            hkSimplexSolverInput& input)
    {
        MemoryContextScope context(0x32);
        ResetContactState();
        m_supportNorm.setZero4();
        static_cast<bhkCharacterController*>(this)->m_flags &= ~4u;
        if (IsFlagSet(16)) SetSupportBody(HK_NULL);
        m_supportCount = 0;
        int  numVertical        = 0;
        int  numVerticalCounted = 0;
        bool haveNormal         = false;
        m_supportNorm           = g_characterUp;

        hkVector4 capsuleTopA;
        hkVector4 capsuleTopB;
        hkReal    capsuleRadiusSq = 0.0f;
        bool      haveCapsules    = false;
        if ((m_flags & 0x80020) == 0x80000) {
            const hkpShapePhantom* phantom = m_shapePhantom->GetPhantom();
            if (phantom && m_shapes[0]) {
                const hkpShape* shape = m_shapes[0]->GetShape();
                if (shape && shape->getType() == HK_SHAPE_LIST) {
                    const hkpListShape* list = static_cast<const hkpListShape*>(shape);
                    if (list->getNumChildShapes() == 3) {
                        const hkpShape* child = list->m_childInfo[1].m_shape;
                        if (child && child->getType() == HK_SHAPE_CAPSULE) {
                            const hkpCapsuleShape* capsule = static_cast<const hkpCapsuleShape*>(child);
                            {
                                hkVector4 top = capsule->m_vertexA;
                                top(2) += capsule->getRadius();
                                capsuleTopA.setTransformedPos(phantom->getTransform(), top);
                            }
                            child = list->m_childInfo[2].m_shape;
                            if (child && child->getType() == HK_SHAPE_CAPSULE) {
                                capsule = static_cast<const hkpCapsuleShape*>(child);
                                hkVector4 top = capsule->m_vertexA;
                                top(2) += capsule->getRadius();
                                capsuleTopB.setTransformedPos(phantom->getTransform(), top);
                                haveCapsules    = true;
                                capsuleRadiusSq = capsule->getRadius() * capsule->getRadius() * 0.110889f;
                            }
                        }
                    }
                }
            }
        }

        const int numPoints  = hkMath::min2(input.m_numConstraints, manifold.getSize());
        bool      animStatic = false;
        for (int i = 0; i < numPoints; i++) {
            hkSurfaceConstraintInfo& constraint = input.m_constraints[i];
            const hkRootCdPoint&     point      = manifold[i];
            hkpRigidBody* const      body       = hkpGetRigidBody(point.m_rootCollidableB);
            const hkUint32           filterInfo = point.m_rootCollidableB->getCollisionFilterInfo();
            if ((m_flags & 0x210000) == 0x210000 && IsHurtfulBody(body)) ProcessHurtfulBody(body, &point.m_contact);
            const hkUint32 layer = filterInfo & 0x7f;
            animStatic |= (layer == 2);
            bool      addVertical = !IsFlagSet(11);
            hkReal    normalZ     = point.m_contact.m_separatingNormal.getComponent<2>().getReal();
            hkVector4 plane       = constraint.m_plane;

            if (body) {
                switch (layer) {
                case 2: {
                    const hkpMotion::MotionType type = body->getMotionType();
                    if (type == hkpMotion::MOTION_KEYFRAMED || type == hkpMotion::MOTION_FIXED) constraint.m_extraDownStaticFriction = 0.577f;
                    break;
                }
                case 4:
                case 5:
                case 6:
                    if (bhkIsDynamicMotionType(body->getMotionType())) {
                        if (body->getMass() < g_fMoveLimitMass) {
                            constraint.m_plane.setZero4();
                            constraint.m_velocity.setZero4();
                        }
                        if (!IsFlagSet(16)) {
                            plane.set(0.0f, 0.0f, 1.0f, 0.0f);
                            normalZ = 1.0f;
                        }
                        addVertical = false;
                    }
                    break;
                case 10:
                    if (bhkIsDynamicMotionType(body->getMotionType())) {
                        if (!IsFlagSet(16)) {
                            plane.set(0.0f, 0.0f, 1.0f, 0.0f);
                            normalZ                      = 1.0f;
                            constraint.m_dynamicFriction = 0.0f;
                            constraint.m_velocity.setZero4();
                        }
                        addVertical = false;
                        break;
                    }
                default:
                    if (haveCapsules && !IsFlagSet(5)) {
                        hkVector4 toA;
                        toA.setSub4(point.m_contact.m_position, capsuleTopA);
                        hkVector4 toB;
                        if (toA.lengthSquared3().getReal() < capsuleRadiusSq ||
                            (toB.setSub4(point.m_contact.m_position, capsuleTopB), toB.lengthSquared3().getReal() < capsuleRadiusSq)) {
                            m_flags |= 0x20;
                        }
                    }
                    break;
                case 30: {
                    bhkCharacterController* other = GetControllerFromCollidable(body);
                    if (other) {
                        constraint.m_plane(2) = 0.0f;
                        constraint.m_plane.normalize3IfNotZero();
                        PushCharacter(other, point.m_contact);
                        if (bhkCharacterCollisionHandler::s_singleton) bhkCharacterCollisionHandler::s_singleton->HandleCharacterCollision(this, other);
                    }
                    break;
                }
                }

                if (layer != 30 && normalZ >= 0.0f) {
                    if (normalZ < m_zNormal) {
                        if (!TryStep(point)) {
                            const bool counted = (!addVertical && !IsFlagSet(11)) || normalZ >= 0.2f;
                            if (numVertical < 4 && addVertical) {
                                hkSurfaceConstraintInfo& vertical = input.m_constraints[input.m_numConstraints];
                                vertical = constraint;
                                vertical.m_velocity.setZero4();
                                vertical.m_plane(2) = 0.0f;
                                vertical.m_plane.mul4(hkMath::sqrtInverseOfSplat(vertical.m_plane.lengthSquared3()));
                                input.m_numConstraints++;
                                numVertical++;
                                if (counted) numVerticalCounted++;
                            }
                            if (!counted) continue;
                        } else if (!m_highSpeed) {
                            if (!(m_flags & 0x1800)) constraint.m_dynamicFriction = 0.0f;
                            m_flags |= 4;
                            const hkpCharacterStateType previous = m_context.m_previousState;
                            if (m_context.getState() == previous) m_flags |= 0x400;
                            plane(2) = 1.0f;
                            plane.mul4(hkMath::sqrtInverseOfSplat(plane.lengthSquared3()));
                            constraint.m_plane = plane;
                        }
                    } else {
                        if (!IsFlagSet(16)) m_orientationCtrl->OnSupportContact(point);
                        const hkpCharacterStateType previous = m_context.m_previousState;
                        if (m_context.getState() == previous) m_flags |= 0x400;
                        if (!IsFlagSet(3)) constraint.m_extraDownStaticFriction = 5.7700005f;
                    }

                    m_flags |= 0x200;
                    m_supportCount++;
                    UpdateContactMaterial(manifold[i]);
                    if (haveNormal) plane.add4(m_supportNorm);
                    else            haveNormal = true;
                    m_supportNorm = plane;
                    if (IsFlagSet(16)) SetSupportBody(body);
                }
            } else if (bhkCollisionFilter::GetSingleton()->m_sensorField & (1ull << layer)) {
                constraint.m_velocity.setZero4();
            } else {
                switch (layer) {
                case 14:
                    if (IsFlagSet(16)) constraint.m_velocity.setZero4();
                    break;
                case 30: {
                    bhkCharacterController* other =
                        GetControllerFromCollidable(static_cast<const hkpWorldObject*>(point.m_rootCollidableB->getOwner()));
                    if (other && (other->m_flags & 0x8000000)) {
                        constraint.m_plane.setZero4();
                        constraint.m_velocity.setZero4();
                    } else {
                        constraint.m_plane(2) = 0.0f;
                        constraint.m_plane.normalize3IfNotZero();
                    }
                    break;
                }
                }
            }
        }

        if (animStatic) m_flags |= 0x20000000;
        else            m_flags &= ~0x20000000u;
        m_onlyWallContacts = (m_supportCount == numVerticalCounted);
        m_supportNorm.normalize3IfNotZero();
    }
} }
