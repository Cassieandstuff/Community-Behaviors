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
        /// @brief The body driving ragdoll bone `boneId`, null if none.
        HK_FORCE_INLINE hkpRigidBody* getRigidBodyOfBone(int boneId) const
        {
            const int rbIndex = m_boneToRigidBodyMap[boneId];
            return (rbIndex >= 0) ? m_rigidBodies[rbIndex] : HK_NULL;
        }
    };
} }
