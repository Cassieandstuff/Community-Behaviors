/// @file
/// @brief bhkCharacterController functions still being matched (see tools/cbmatch/parked.yaml).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharRigidBodyController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <Physics/Collide/Shape/Compound/Collection/hkpShapeCollection.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Physics/Collide/Shape/Convex/Triangle/hkpTriangleShape.h>
#include <Physics/Collide/Shape/Query/hkpShapeRayCastInput.h>
#include <Physics/Collide/Shape/Query/hkpShapeRayCastOutput.h>
#include <Physics/Collide/Util/hkCollideCapsuleUtil.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>

namespace CB { namespace havok
{
    float bhkCharacterController::GetFallDistance()
    {
        if (m_fallStartHeight != 0.0f) {
            hkVector4 position;
            GetPositionImpl(position, true);
            const float height = position(2) * bhkWorld::s_worldScaleInverse;
            return hkMath::max2(m_fallStartHeight - height, 0.0f);
        }
        return 0.0f;
    }

    /// @brief `numVectors` points of `vectorsIn` placed by `t` into `vectorsOut`, last first.
    /// @note Havok's hkVector4Util::transformPoints, inlined here as vanilla has it (the transform held in registers,
    ///       each point (c0·x + c1·y) + (c2·z + t)).
    static HK_FORCE_INLINE void transformPoints(const hkTransform& t, const hkVector4* vectorsIn, int numVectors, hkVector4* vectorsOut)
    {
        const hkTransform unaliased = t;
        int i = numVectors - 1;
        do {
            const hkQuadReal v  = vectorsIn[i].m_quad;
            const hkQuadReal xy = _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1)), unaliased.m_rotation.m_col1.m_quad),
                                             _mm_mul_ps(_mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0)), unaliased.m_rotation.m_col0.m_quad));
            const hkQuadReal zt = _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2)), unaliased.m_rotation.m_col2.m_quad),
                                             unaliased.m_translation.m_quad);
            vectorsOut[i].m_quad = _mm_add_ps(xy, zt);
        } while (--i >= 0);
    }

    /// @brief Whether `point`'s body B is a mesh whose material (at the contact's shape key) is a stairs one.
    static HK_FORCE_INLINE bool IsStairsContact(const hkRootCdPoint& point)
    {
        const hkpShape*   mesh    = bhkGetMeshShape(point.m_rootCollidableB->getShape());
        const hkpShapeKey key     = point.m_shapeKeyB;
        const bhkShape*   wrapper = mesh ? reinterpret_cast<const bhkShape*>(mesh->getUserData()) : HK_NULL;
        bool              stairs  = false;
        if (wrapper) stairs = bhkIsStairsMaterial(key != HK_INVALID_SHAPE_KEY ? wrapper->GetMaterialID(key) : wrapper->m_materialID);
        return stairs;
    }

    bool bhkCharacterController::TryStep(const hkRootCdPoint& point)
    {
        const void*         ownerA  = point.m_rootCollidableA->getOwner();
        const hkpRigidBody* bodyB   = hkpGetRigidBody(point.m_rootCollidableB);
        bool                canStep = false;
        bool                reject  = false;
        hkpShapeKey         hitKey  = HK_INVALID_SHAPE_KEY;
        if (bodyB) canStep = IsStairsContact(point);
        if (!canStep && ownerA && bodyB) {
            const hkpShape*    shapeB = point.m_rootCollidableB->getShape();
            hkReal             radius = m_radius;
            const hkpShape*    shapeA = point.m_rootCollidableA->getShape();
            const hkpShapeType typeA  = shapeA->getType();
            if (typeA == HK_SHAPE_CAPSULE) radius = static_cast<const hkpCapsuleShape*>(shapeA)->getRadius();
            hkVector4    contact  = point.m_contact.m_position;
            const hkReal contactZ = contact.getComponent<2>().getReal();
            hkVector4    position;
            hkVector4    to;
            GetPositionImpl(position, true);
            const int probe = m_orientationCtrl->GetStepProbe(this, typeA == HK_SHAPE_CAPSULE ? static_cast<const hkpCapsuleShape*>(shapeA) : HK_NULL,
                                                              contact, position);
            hkVector4 direction;
            direction.setSub4(position, contact);
            direction(2) = 0.0f;
            direction.normalize3();
            const hkReal facing = point.m_contact.m_separatingNormal.dot3(direction).getReal();
            if (facing < 0.0f && !hkpGetRigidBody(point.m_rootCollidableA)) reject = true;
            const hkReal stepHeight = m_stepHeight;
            if (probe == -1) position(2) += stepHeight;
            contact(2) = position.getComponent<2>().getReal();
            const hkReal height = contactZ - position.getComponent<2>().getReal();
            bool         stepUp = height > 0.0f;
            const hkReal maxHeight = stepHeight * (1.0f / 3.0f);
            if (height > maxHeight && !reject) reject = m_orientationCtrl->RejectStep(this, maxHeight, contactZ, probe, stepUp);
            if (!reject) {
                const hkpShape* meshB;
                if (point.m_shapeKeyB != HK_INVALID_SHAPE_KEY && (meshB = bhkGetMeshShape(shapeB)) != HK_NULL) {
                    hkpShapeBuffer          buffer;
                    const hkpTriangleShape* triangle = static_cast<const hkpTriangleShape*>(
                        static_cast<const hkpShapeCollection*>(meshB)->getChildShape(point.m_shapeKeyB, buffer));
                    hkVector4 vertices[3];
                    transformPoints(bodyB->getTransform(), &triangle->m_vertexA, 3, vertices);
                    hkCollideTriangleUtil::PointTriangleDistanceCache cache;
                    hkCollideTriangleUtil::setupPointTriangleDistanceCache(vertices, cache);
                    hkVector4 segment[2];
                    segment[0] = contact;
                    segment[1] = contact;
                    segment[1](0) -= direction.getComponent<0>().getReal() * m_stepProbeDistance;
                    segment[1](1) -= direction.getComponent<1>().getReal() * m_stepProbeDistance;
                    hkContactPoint points[3];
                    hkCollideCapsuleUtilCapsVsTri(segment, 0.05f, vertices, triangle->getRadius(), cache, 0.0f, 0, points, HK_NULL);
                    if (points[0].getDistance() < 0.0f || points[1].getDistance() < 0.0f) return false;
                    canStep = true;
                } else {
                    hkpShapeRayCastInput  input;
                    hkpShapeRayCastOutput output;
                    to = position;
                    const hkReal          reach = radius + m_stepProbeDistance;
                    to(0) -= direction.getComponent<0>().getReal() * reach;
                    to(1) -= direction.getComponent<1>().getReal() * reach;
                    hkTransform transform = bodyB->getTransform();
                    input.m_from.setTransformedInversePos(transform, position);
                    input.m_to.setTransformedInversePos(transform, to);
                    shapeB->castRay(input, output);
                    if (!output.hasHit()) {
                        canStep = true;
                    } else {
                        hkLocalBuffer<hkpShapeBuffer> buffers(hkpShapeRayCastOutput::MAX_HIERARCHY_DEPTH);
                        hkLocalArray<const hkpShape*> shapes(hkpShapeRayCastOutput::MAX_HIERARCHY_DEPTH + 1);
                        shapes.pushBack(shapeB);
                        bool viaMopp = false;
                        int  i       = 0;
                        for (; i < hkpShapeRayCastOutput::MAX_HIERARCHY_DEPTH; i++) {
                            if (output.m_shapeKeys[i] == HK_INVALID_SHAPE_KEY) break;
                            if (!viaMopp && shapes[i]->getType() == HK_SHAPE_MOPP) {
                                shapes.pushBack(shapes[i]);
                                viaMopp = true;
                            } else {
                                viaMopp = false;
                                if (shapes[i]->getContainer()) shapes.pushBack(shapes[i]->getContainer()->getChildShape(output.m_shapeKeys[i], buffers[i]));
                            }
                        }
                        const hkpShape* hitShape = shapeB;
                        if (i != 0) {
                            hitKey   = output.m_shapeKeys[i - 1];
                            hitShape = shapes[shapes.getSize() - 2];
                        }
                        canStep = !IsSlopeBlocking(hitShape, true, stepUp, hitKey);
                    }
                }
            }
        }
        if (canStep) m_orientationCtrl->OnSupportContact(point);
        return canStep;
    }
} }
