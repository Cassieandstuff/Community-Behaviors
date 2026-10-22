/// @file
/// @brief hkaRagdollInstance: a ragdoll's rigid bodies, constraints and the bone → body map.
#pragma once

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace havok
{
    class hkpRigidBody;
    class hkpConstraintInstance;
    class hkpWorld;

    /// @note 0x48 bytes (schema/physics/hkaRagdollInstance.yaml). Names of the pose functions the Havok SDK documents
    ///       (getPoseWorldSpace, setPoseWorldSpace, getPoseModelSpace, setPoseModelSpace, addToWorld, removeFromWorld,
    ///       clone) are Havok's; the others are provisional.
    class hkaRagdollInstance : public hkReferencedObject
    {
    public:
        hkArray<hkpRigidBody*>          m_rigidBodies;          ///< +0x10 — each referenced
        hkArray<hkpConstraintInstance*> m_constraints;          ///< +0x20 — each referenced
        hkArray<int>                    m_boneToRigidBodyMap;   ///< +0x30 — -1 for a bone without a body
        hkRefPtr<const hkaSkeleton>     m_skeleton;             ///< +0x40

        /// @brief A ragdoll whose body i drives bone i.
        hkaRagdollInstance(const hkArray<hkpRigidBody*>& rigidBodies, const hkArray<hkpConstraintInstance*>& constraints, const hkaSkeleton* skeleton);
        hkaRagdollInstance(const hkArray<hkpRigidBody*>& rigidBodies, const hkArray<hkpConstraintInstance*>& constraints, const hkaSkeleton* skeleton,
                           const hkArray<int>& boneToRigidBodyMap);
        hkaRagdollInstance(hkFinishLoadedObjectFlag flag);
        ~hkaRagdollInstance();

        /// @brief A copy with cloned bodies and constraints (each constraint between the clones of its bodies); `mode` is an
        ///        hkpConstraintInstance::CloningMode.
        virtual hkaRagdollInstance* clone(int mode) const;   ///< slot 3

        HK_FORCE_INLINE const hkaSkeleton* getSkeleton() const { return m_skeleton; }
        HK_FORCE_INLINE int                getNumBones() const { return m_skeleton->m_bones.getSize(); }
        /// @brief The body driving ragdoll bone `boneId`, null if none.
        HK_FORCE_INLINE hkpRigidBody* getRigidBodyOfBone(int boneId) const
        {
            const int rbIndex = m_boneToRigidBodyMap[boneId];
            return (rbIndex >= 0) ? m_rigidBodies[rbIndex] : HK_NULL;
        }
        /// @brief The ragdoll bone `rigidBody` drives, -1 if it is not one of the ragdoll's bodies.
        /// @note Out of line in vanilla: a copy among hkbRagdollDriver's functions (0x140ab0980).
        int getBoneIndexOfRigidBody(const hkpRigidBody* rigidBody) const;

        /// @brief Adds the bodies and constraints to `world`, re-filtering each body when `updateFilter` is set.
        /// @return HK_FAILURE (adding nothing) when the first body is already in a world.
        hkResult addToWorld(hkpWorld* world, hkBool updateFilter) const;
        /// @brief Removes the constraints that are in a world, then the bodies.
        /// @return HK_FAILURE when the first body is in no world.
        hkResult removeFromWorld() const;

        /// @brief The bodies' transforms as a world-space pose: bodiless bones follow their parent by
        ///        `localSpacePoseForUnmappedBones` (default the reference pose), roots by `worldFromModel`.
        void getPoseWorldSpace(hkQsTransform* poseWorldSpaceOut, const hkQsTransform& worldFromModel,
                               const hkQsTransform* localSpacePoseForUnmappedBones = HK_NULL) const;
        /// @brief getPoseWorldSpace with the identity as worldFromModel.
        void getPoseWorldSpace(hkQsTransform* poseWorldSpaceOut) const;
        /// @brief The world-space pose with each body's transform approximated at the world's current time.
        /// @note Name provisional; without a world (or bodies) it is getPoseWorldSpace.
        void getApproxCurrentPoseWorldSpace(hkQsTransform* poseWorldSpaceOut) const;
        /// @brief The world-space pose with each body's transform approximated at `time`.
        /// @note Name provisional.
        void getApproxPoseWorldSpaceAt(hkTime time, hkQsTransform* poseWorldSpaceOut) const;
        /// @brief Bone `boneId`'s world transform: its body's, else its parent's composed with the reference pose.
        /// @note Name provisional.
        void getBoneWorldSpace(int boneId, hkQsTransform& boneWorldSpaceOut) const;
        /// @brief getBoneWorldSpace with the bodies' transforms approximated at `time`.
        /// @note Name provisional.
        void getApproxBoneWorldSpaceAt(int boneId, hkTime time, hkQsTransform& boneWorldSpaceOut) const;
        /// @brief Teleports each body to its bone's transform in `poseWorldSpaceIn`, zeroing its velocities.
        void setPoseWorldSpace(const hkQsTransform* poseWorldSpaceIn);
        /// @brief The ragdoll's current pose in the model space `worldFromModel` defines.
        /// @note Out of line in vanilla (0x140bd75f0).
        void getPoseModelSpace(hkQsTransform* poseModelSpaceOut, const hkQsTransform& worldFromModel) const;
        /// @brief setPoseWorldSpace for a model-space pose placed by `worldFromModel` (rotations renormalized).
        void setPoseModelSpace(const hkQsTransform* poseModelSpaceIn, const hkQsTransform& worldFromModel);
        /// @brief Teleports each body to its bone's transform in `poseModelSpaceIn` placed by `worldFromModel`, and sets
        ///        the velocities that would have carried it there from the previous pose over `timestep`.
        /// @note Name provisional.
        void setPoseModelSpaceKeyframed(const hkQsTransform* previousPoseModelSpace, const hkQsTransform& previousWorldFromModel,
                                        const hkQsTransform* poseModelSpaceIn, const hkQsTransform& worldFromModel, hkReal timestep);

    private:
        /// @brief References the skeleton, copies both arrays and references every body and constraint.
        /// @note Name provisional.
        void init(const hkArray<hkpRigidBody*>& rigidBodies, const hkArray<hkpConstraintInstance*>& constraints, const hkaSkeleton* skeleton);
    };
} }
