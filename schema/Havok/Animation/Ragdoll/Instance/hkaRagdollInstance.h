/// @file
/// @brief hkaRagdollInstance: a ragdoll's rigid bodies and the bone → body map.
#pragma once

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    class hkpRigidBody;

    class hkaRagdollInstance : public hkReferencedObject
    {
    public:
        hkArray<hkpRigidBody*>      m_rigidBodies;          ///< +0x10
        hkArray<void*>              m_constraints;          ///< +0x20 — hkpConstraintInstance*
        hkArray<int>                m_boneToRigidBodyMap;   ///< +0x30
        hkRefPtr<const hkaSkeleton> m_skeleton;             ///< +0x40

        HK_FORCE_INLINE const hkaSkeleton* getSkeleton() const { return m_skeleton; }
        /// @brief The ragdoll bone `rigidBody` drives, -1 if it is not one of the ragdoll's bodies.
        /// @note Out of line in vanilla; name per the Havok SDK.
        int getBoneIndexOfRigidBody(const hkpRigidBody* rigidBody) const;
        /// @brief The ragdoll's current pose (its bodies' transforms) in the model space `worldFromModel` defines.
        /// @note Name per the Havok SDK; out of line in vanilla (0x140bd75f0).
        void getPoseModelSpace(hkQsTransform* poseModelSpaceOut, const hkQsTransform& worldFromModel) const;
        /// @brief The body driving ragdoll bone `boneId`, null if none.
        HK_FORCE_INLINE hkpRigidBody* getRigidBodyOfBone(int boneId) const
        {
            const int rbIndex = m_boneToRigidBodyMap[boneId];
            return (rbIndex >= 0) ? m_rigidBodies[rbIndex] : HK_NULL;
        }
    };
} }
