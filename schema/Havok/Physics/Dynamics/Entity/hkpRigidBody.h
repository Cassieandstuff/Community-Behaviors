/// @file
/// @brief hkpRigidBody: only the members the behavior runtime reads (the physics reimplementation is a later pass).
#pragma once

#include <Common/Base/Types/Geometry/LocalFrame/hkLocalFrame.h>

namespace CB { namespace schema
{
    class hkpWorld;

    /// @brief A world object's user property: a key and a 64-bit value.
    struct hkpProperty
    {
        hkUint32 m_key;                ///< +0x00
        hkUint32 m_alignmentPadding;   ///< +0x04
        hkUlong  m_value;              ///< +0x08 — hkpPropertyValue

        HK_FORCE_INLINE void* getPtr() const { return reinterpret_cast<void*>(m_value); }
    };

    /// @note Offsets per CommonLib's hkpWorldObject/hkpEntity layout: the collidable's collision filter info (+0x4c),
    ///       the properties (+0xb8), the motion state's transform (+0x170) and linear velocity (+0x230), and the local
    ///       frame (+0x2b0).
    class hkpRigidBody : public hkReferencedObject
    {
    public:
        /// @note Values per the Havok SDK.
        enum MotionType { MOTION_KEYFRAMED = 4 };
        enum QualityType { HK_COLLIDABLE_QUALITY_KEYFRAMED = 1, HK_COLLIDABLE_QUALITY_MOVING = 4 };
        enum EntityActivation { HK_ENTITY_ACTIVATION_DO_NOT_ACTIVATE = 0, HK_ENTITY_ACTIVATION_DO_ACTIVATE = 1 };

        hkpWorld*              m_world;                 ///< +0x10 — hkpWorldObject::m_world
        char                   m_pad18[0x4a - 0x18];
        /// @note hkpCollidableQualityType: 1 keyframed, 4 moving.
        hkInt8                 m_objectQualityType;     ///< +0x4a — m_collidable.m_broadPhaseHandle.m_objectQualityType
        char                   m_pad4B[0x4c - 0x4b];
        hkUint32               m_collisionFilterInfo;   ///< +0x4c — m_collidable.m_broadPhaseHandle.m_collisionFilterInfo
        char                   m_pad50[0xb8 - 0x50];
        hkArray<hkpProperty>   m_properties;            ///< +0xb8
        char                   m_padC8[0x160 - 0xc8];
        /// @note hkpMotion::MotionType: 4 keyframed.
        hkUint8                m_motionType;            ///< +0x160 — m_motion.m_type
        char                   m_pad161[0x170 - 0x161];
        hkTransform            m_transform;             ///< +0x170 — m_motion.m_motionState.m_transform
        char                   m_pad1B0[0x230 - 0x1b0];
        hkVector4              m_linearVelocity;        ///< +0x230 — m_motion.m_linearVelocity
        char                   m_pad240[0x2b0 - 0x240];
        hkRefPtr<hkLocalFrame> m_localFrame;            ///< +0x2b0

        /// @brief Switches the body to `newState` (a hkpMotion::MotionType).
        /// @note Out of line in vanilla (hkp code, not reimplemented); parameters per the Havok SDK.
        void setMotionType(int newState, int preferredActivationState = HK_ENTITY_ACTIVATION_DO_ACTIVATE);

        HK_FORCE_INLINE hkUint32           getCollisionFilterInfo() const { return m_collisionFilterInfo; }
        HK_FORCE_INLINE const hkTransform& getTransform() const { return m_transform; }
        HK_FORCE_INLINE const hkVector4&   getPosition() const { return m_transform.m_translation; }
        HK_FORCE_INLINE const hkVector4&   getLinearVelocity() const { return m_linearVelocity; }
        HK_FORCE_INLINE hkLocalFrame*      getLocalFrame() const { return m_localFrame; }

        /// @brief The property's value, 0 if the body has none under `key`.
        HK_FORCE_INLINE hkUlong getProperty(hkUint32 key) const
        {
            for (int i = 0; i < m_properties.getSize(); i++) {
                if (m_properties[i].m_key == key) return m_properties[i].m_value;
            }
            return 0;
        }
    };
} }
