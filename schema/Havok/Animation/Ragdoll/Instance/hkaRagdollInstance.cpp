/// @file
/// @brief hkaRagdollInstance, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>

#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Physics/Dynamics/Constraint/hkpConstraintInstance.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    hkaRagdollInstance::hkaRagdollInstance(const hkArray<hkpRigidBody*>& rigidBodies, const hkArray<hkpConstraintInstance*>& constraints,
                                           const hkaSkeleton* skeleton)
    {
        init(rigidBodies, constraints, skeleton);
        m_boneToRigidBodyMap.setSize(skeleton->m_bones.getSize());
        for (int i = 0; i < skeleton->m_bones.getSize(); i++) {
            m_boneToRigidBodyMap[i] = i;
        }
    }

    hkaRagdollInstance::hkaRagdollInstance(const hkArray<hkpRigidBody*>& rigidBodies, const hkArray<hkpConstraintInstance*>& constraints,
                                           const hkaSkeleton* skeleton, const hkArray<int>& boneToRigidBodyMap)
    {
        init(rigidBodies, constraints, skeleton);
        m_boneToRigidBodyMap = boneToRigidBodyMap;
    }

    hkaRagdollInstance::~hkaRagdollInstance()
    {
        for (int i = 0; i < m_rigidBodies.getSize(); i++) {
            m_rigidBodies[i]->removeReference();
        }
        for (int i = 0; i < m_constraints.getSize(); i++) {
            m_constraints[i]->removeReference();
        }
    }

    hkaRagdollInstance* hkaRagdollInstance::clone(int mode) const
    {
        const int numRigidBodies = m_rigidBodies.getSize();
        const int numConstraints = m_constraints.getSize();

        hkLocalArray<hkpRigidBody*> newRigidBodies(numRigidBodies);
        newRigidBodies.setSize(numRigidBodies);
        hkLocalArray<hkpConstraintInstance*> newConstraints(numConstraints);
        newConstraints.setSize(numConstraints);

        hkPointerMap<hkpRigidBody*, hkpRigidBody*> rigidBodyMap;
        for (int i = 0; i < numRigidBodies; i++) {
            newRigidBodies[i] = m_rigidBodies[i]->clone();
            rigidBodyMap.insert(m_rigidBodies[i], newRigidBodies[i]);
        }
        for (int i = 0; i < numConstraints; i++) {
            const hkpConstraintInstance* constraint = m_constraints[i];
            hkpRigidBody* rigidBodyA = rigidBodyMap.getWithDefault(constraint->getRigidBodyA(), HK_NULL);
            hkpRigidBody* rigidBodyB = rigidBodyMap.getWithDefault(constraint->getRigidBodyB(), HK_NULL);
            newConstraints[i] = m_constraints[i]->clone(reinterpret_cast<hkpEntity*>(rigidBodyA), reinterpret_cast<hkpEntity*>(rigidBodyB), mode);
        }

        hkaRagdollInstance* newInstance = new hkaRagdollInstance(newRigidBodies, newConstraints, m_skeleton, m_boneToRigidBodyMap);

        for (int i = 0; i < numRigidBodies; i++) {
            newRigidBodies[i]->removeReference();
        }
        for (int i = 0; i < numConstraints; i++) {
            newConstraints[i]->removeReference();
        }
        return newInstance;
    }

    void hkaRagdollInstance::getPoseWorldSpace(hkQsTransform* poseWorldSpaceOut) const
    {
        getPoseWorldSpace(poseWorldSpaceOut, hkQsTransform::getIdentity(), HK_NULL);
    }

    void hkaRagdollInstance::getApproxCurrentPoseWorldSpace(hkQsTransform* poseWorldSpaceOut) const
    {
        if (m_rigidBodies[0] && m_rigidBodies[0]->getWorld()) {
            getApproxPoseWorldSpaceAt(m_rigidBodies[0]->getWorld()->getCurrentTime(), poseWorldSpaceOut);
            return;
        }
        getPoseWorldSpace(poseWorldSpaceOut);
    }

    void hkaRagdollInstance::getApproxPoseWorldSpaceAt(hkTime time, hkQsTransform* poseWorldSpaceOut) const
    {
        const int numBones = m_skeleton->m_bones.getSize();
        for (int b = 0; b < numBones; b++) {
            getApproxBoneWorldSpaceAt(b, time, poseWorldSpaceOut[b]);
        }
    }

    void hkaRagdollInstance::setPoseWorldSpace(const hkQsTransform* poseWorldSpaceIn)
    {
        const int numBones = m_skeleton->m_bones.getSize();
        for (int b = 0; b < numBones; b++) {
            hkpRigidBody* rb = getRigidBodyOfBone(b);
            if (rb) {
                rb->setPositionAndRotation(poseWorldSpaceIn[b].m_translation, poseWorldSpaceIn[b].m_rotation);
                rb->setLinearVelocity(hkVector4::getZero());
                rb->setAngularVelocity(hkVector4::getZero());
            }
        }
    }

    void hkaRagdollInstance::setPoseModelSpace(const hkQsTransform* poseModelSpaceIn, const hkQsTransform& worldFromModel)
    {
        const int numBones = m_skeleton->m_bones.getSize();
        for (int b = 0; b < numBones; b++) {
            hkpRigidBody* rb = getRigidBodyOfBone(b);
            if (rb) {
                hkQsTransform boneWorldSpace;
                boneWorldSpace.setMul(worldFromModel, poseModelSpaceIn[b]);
                boneWorldSpace.m_rotation.normalize();
                rb->setPositionAndRotation(boneWorldSpace.m_translation, boneWorldSpace.m_rotation);
                rb->setLinearVelocity(hkVector4::getZero());
                rb->setAngularVelocity(hkVector4::getZero());
            }
        }
    }

    hkResult hkaRagdollInstance::addToWorld(hkpWorld* world, hkBool updateFilter) const
    {
        if (m_rigidBodies[0] && m_rigidBodies[0]->getWorld()) return HK_FAILURE;

        world->addEntityBatch(reinterpret_cast<hkpEntity* const*>(m_rigidBodies.begin()), m_rigidBodies.getSize(), HK_ENTITY_ACTIVATION_DO_ACTIVATE);
        for (int i = 0; i < m_constraints.getSize(); i++) {
            world->addConstraint(m_constraints[i]);
        }
        if (updateFilter) {
            const int numRigidBodies = m_rigidBodies.getSize();
            for (int i = 0; i < numRigidBodies; i++) {
                world->updateCollisionFilterOnEntity(m_rigidBodies[i], HK_UPDATE_FILTER_ON_ENTITY_DISABLE_ENTITY_ENTITY_COLLISIONS_ONLY,
                                                     HK_UPDATE_COLLECTION_FILTER_IGNORE_SHAPE_COLLECTIONS);
            }
        }
        return HK_SUCCESS;
    }

    hkResult hkaRagdollInstance::removeFromWorld() const
    {
        if (m_rigidBodies[0] == HK_NULL) return HK_FAILURE;
        hkpWorld* world = m_rigidBodies[0]->getWorld();
        if (world == HK_NULL) return HK_FAILURE;

        for (int i = 0; i < m_constraints.getSize(); i++) {
            if (m_constraints[i]->getOwner() != HK_NULL) world->removeConstraint(m_constraints[i]);
        }
        world->removeEntityBatch(reinterpret_cast<hkpEntity* const*>(m_rigidBodies.begin()), m_rigidBodies.getSize());
        return HK_SUCCESS;
    }

    void hkaRagdollInstance::getBoneWorldSpace(int boneId, hkQsTransform& boneWorldSpaceOut) const
    {
        const hkpRigidBody* rb = getRigidBodyOfBone(boneId);
        if (rb) {
            boneWorldSpaceOut.setFromTransformNoScale(rb->getTransform());
            hkaSkeletonUtils::getModelSpaceScale(*m_skeleton, m_skeleton->m_referencePose.begin(), boneId, boneWorldSpaceOut.m_scale);
            return;
        }

        const int     parent = m_skeleton->m_parentIndices[boneId];
        hkQsTransform parentWorldSpace;
        if (parent < 0) parentWorldSpace.setIdentity();
        else            getBoneWorldSpace(parent, parentWorldSpace);
        boneWorldSpaceOut.setMul(parentWorldSpace, m_skeleton->m_referencePose[boneId]);
    }

    void hkaRagdollInstance::getApproxBoneWorldSpaceAt(int boneId, hkTime time, hkQsTransform& boneWorldSpaceOut) const
    {
        const hkpRigidBody* rb = getRigidBodyOfBone(boneId);
        if (rb) {
            hkTransform transform;
            rb->approxTransformAt(time, transform);
            boneWorldSpaceOut.setFromTransformNoScale(transform);
            hkaSkeletonUtils::getModelSpaceScale(*m_skeleton, m_skeleton->m_referencePose.begin(), boneId, boneWorldSpaceOut.m_scale);
            return;
        }

        const int     parent = m_skeleton->m_parentIndices[boneId];
        hkQsTransform parentWorldSpace;
        if (parent < 0) parentWorldSpace.setIdentity();
        else            getApproxBoneWorldSpaceAt(parent, time, parentWorldSpace);
        boneWorldSpaceOut.setMul(parentWorldSpace, m_skeleton->m_referencePose[boneId]);
    }

    hkaRagdollInstance::hkaRagdollInstance(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_rigidBodies(flag),
        m_constraints(flag),
        m_boneToRigidBodyMap(flag),
        m_skeleton(flag)
    {
    }

    void hkaRagdollInstance::init(const hkArray<hkpRigidBody*>& rigidBodies, const hkArray<hkpConstraintInstance*>& constraints, const hkaSkeleton* skeleton)
    {
        m_skeleton    = skeleton;
        m_rigidBodies = rigidBodies;
        m_constraints = constraints;
        for (int i = 0; i < m_rigidBodies.getSize(); i++) {
            if (m_rigidBodies[i]) m_rigidBodies[i]->addReference();
        }
        for (int i = 0; i < m_constraints.getSize(); i++) {
            if (m_constraints[i]) m_constraints[i]->addReference();
        }
    }
} }
