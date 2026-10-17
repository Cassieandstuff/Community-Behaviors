/// @file
/// @brief Workbench: the functions of hkaRagdollInstance.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Animation/Ragdoll/Instance/hkaRagdollInstance.cpp.
#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Physics/Dynamics/Constraint/hkpConstraintInstance.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    void hkaRagdollInstance::getPoseModelSpace(hkQsTransform* poseModelSpaceOut, const hkQsTransform& worldFromModel) const
    {
        getPoseWorldSpace(poseModelSpaceOut);

        hkQsTransform modelFromWorld;
        modelFromWorld.setInverse(worldFromModel);
        const int numBones = m_skeleton->m_bones.getSize();
        for (int b = 0; b < numBones; b++) {
            poseModelSpaceOut[b].setMul(modelFromWorld, poseModelSpaceOut[b]);
        }
    }

    void hkaRagdollInstance::setPoseModelSpaceKeyframed(const hkQsTransform* previousPoseModelSpace, const hkQsTransform& previousWorldFromModel,
                                                        const hkQsTransform* poseModelSpaceIn, const hkQsTransform& worldFromModel, hkReal timestep)
    {
        const hkReal invTimestep = 1.0f / timestep;
        const int    numBones    = m_skeleton->m_bones.getSize();
        for (int b = 0; b < numBones; b++) {
            hkpRigidBody* rb = getRigidBodyOfBone(b);
            if (rb) {
                hkQsTransform previousWorldSpace;
                previousWorldSpace.setMul(previousWorldFromModel, previousPoseModelSpace[b]);
                previousWorldSpace.m_rotation.normalize();
                hkQsTransform boneWorldSpace;
                boneWorldSpace.setMul(worldFromModel, poseModelSpaceIn[b]);
                boneWorldSpace.m_rotation.normalize();
                rb->setPositionAndRotation(boneWorldSpace.m_translation, boneWorldSpace.m_rotation);
                {
                    hkVector4 linearVelocity;
                    linearVelocity.setSub4(boneWorldSpace.m_translation, previousWorldSpace.m_translation);
                    linearVelocity.mul4(hkSimdReal(invTimestep));
                    rb->setLinearVelocity(linearVelocity);
                }
                {
                    hkQuaternion rotation;
                    rotation.setMulInverse(boneWorldSpace.m_rotation, previousWorldSpace.m_rotation);
                    rotation.normalize();
                    hkVector4 angularVelocity;
                    if (!rotation.hasValidAxis()) {
                        angularVelocity.setZero4();
                    } else {
                        const hkReal angle = rotation.getAngle();
                        hkVector4    axis;
                        rotation.getAxis(axis);
                        angularVelocity.setMul4(hkSimdReal(angle * invTimestep), axis);
                    }
                    rb->setAngularVelocity(angularVelocity);
                }
            }
        }
    }

} }
