/// @file
/// @brief hkpWorldObject: what entities and phantoms share — the world, the collidable, the properties.
#pragma once

#include <Physics/Collide/Agent3/Machine/Nn/hkpLinkedCollidable.h>
#include <Physics/Dynamics/Common/hkpProperty.h>
#include <Common/Base/Thread/MultiThreadCheck/hkMultiThreadCheck.h>
#include <Common/Base/Types/Physics/MotionState/hkMotionState.h>
#include <Common/Base/Container/String/hkString.h>

namespace CB { namespace havok
{
    class hkpWorld;

    /// @note 0xd0 bytes (schema/physics/hkpWorldObject.yaml). Virtual slots 3–5 per the Havok SDK, unconfirmed (nothing
    ///       matched calls them); they place hkpRigidBody::clone at slot 7, which is confirmed.
    class hkpWorldObject : public hkReferencedObject
    {
    public:
        hkpWorld*            m_world;              ///< +0x10
        hkUlong              m_userData;           ///< +0x18
        hkpLinkedCollidable  m_collidable;         ///< +0x20
        hkMultiThreadCheck   m_multiThreadCheck;   ///< +0xa0
        hkStringPtr          m_name;               ///< +0xb0
        hkArray<hkpProperty> m_properties;         ///< +0xb8
        void*                m_treeData;           ///< +0xc8

        virtual int            setShape(const hkpShape* shape);    ///< slot 3 — returns hkWorldOperation::Result
        virtual int            updateShape(void* shapeModifier);   ///< slot 4 — hkpShapeModifier*
        virtual hkMotionState* getMotionState();                   ///< slot 5

        HK_FORCE_INLINE hkpWorld*                  getWorld() const { return m_world; }
        HK_FORCE_INLINE hkUlong                    getUserData() const { return m_userData; }
        HK_FORCE_INLINE const hkpCollidable*       getCollidable() const { return &m_collidable; }
        HK_FORCE_INLINE hkpCollidable*             getCollidableRw() { return &m_collidable; }
        HK_FORCE_INLINE const hkpLinkedCollidable* getLinkedCollidable() const { return &m_collidable; }
        HK_FORCE_INLINE const char*                getName() const { return m_name.cString(); }

        /// @brief The property's value, 0 if the object has none under `key`.
        HK_FORCE_INLINE hkUlong getProperty(hkUint32 key) const
        {
            for (int i = 0; i < m_properties.getSize(); i++) {
                if (m_properties[i].m_key == key) return m_properties[i].m_value;
            }
            return 0;
        }
        /// @brief Sets the property under `key`.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void             setProperty(hkUint32 key, hkpPropertyValue value);
        /// @brief Removes the property under `key`, returning its value.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        hkpPropertyValue removeProperty(hkUint32 key);
    };
} }
