/// @file
/// @brief hkpConstraintInstance: a constraint between two entities (its data, owner, priority, listeners).
#pragma once

#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace havok
{
    class hkpEntity;
    class hkpRigidBody;

    /// @note 0x70 bytes (schema/physics/hkpConstraintInstance.yaml).
    class hkpConstraintInstance : public hkReferencedObject
    {
    public:
        /// @brief How clone() treats the constraint data.
        /// @note Names per the Havok SDK, values unconfirmed.
        enum CloningMode
        {
            CLONE_SHALLOW_IF_NOT_CONSTRAINED_TO_WORLD = 0,
            CLONE_DATAS_WITH_MOTORS                   = 1,
            CLONE_FORCE_SHALLOW                       = 2,
        };

        /// @brief The array of listeners, laid out like hkSmallArray.
        struct SmallArraySerializeOverrideType
        {
            void*    m_data;               ///< +0x00
            hkUint16 m_size;               ///< +0x08
            hkUint16 m_capacityAndFlags;   ///< +0x0a
        };

        void*                           m_owner;                  ///< +0x10 — hkpConstraintOwner*; null when not in a world
        hkpConstraintData*              m_data;                   ///< +0x18
        void*                           m_constraintModifiers;    ///< +0x20 — hkpModifierConstraintAtom*
        hkpEntity*                      m_entities[2];            ///< +0x28
        hkUint8                         m_priority;               ///< +0x38 — ConstraintPriority
        hkBool                          m_wantRuntime;            ///< +0x39
        hkUint8                         m_destructionRemapInfo;   ///< +0x3a — OnDestructionRemapInfo
        SmallArraySerializeOverrideType m_listeners;              ///< +0x40
        hkStringPtr                     m_name;                   ///< +0x50
        hkUlong                         m_userData;               ///< +0x58
        void*                           m_internal;               ///< +0x60 — hkConstraintInternal*
        hkUint32                        m_uid;                    ///< +0x68

        /// @brief A copy of this constraint between `newEntityA` and `newEntityB`; `mode` is a CloningMode.
        /// @note Out of line in vanilla (hkp code, not reimplemented; 0x140b46e20).
        hkpConstraintInstance* clone(hkpEntity* newEntityA, hkpEntity* newEntityB, int mode) const;

        HK_FORCE_INLINE void*              getOwner() const { return m_owner; }
        HK_FORCE_INLINE hkpConstraintData* getData() const { return m_data; }
        HK_FORCE_INLINE hkpEntity*         getEntityA() const { return m_entities[0]; }
        HK_FORCE_INLINE hkpEntity*         getEntityB() const { return m_entities[1]; }
        HK_FORCE_INLINE hkpEntity*         getEntity(int index) const { return m_entities[index]; }
        HK_FORCE_INLINE hkpRigidBody*      getRigidBodyA() const { return reinterpret_cast<hkpRigidBody*>(m_entities[0]); }
        HK_FORCE_INLINE hkpRigidBody*      getRigidBodyB() const { return reinterpret_cast<hkpRigidBody*>(m_entities[1]); }
        HK_FORCE_INLINE hkUlong            getUserData() const { return m_userData; }
    };
} }
