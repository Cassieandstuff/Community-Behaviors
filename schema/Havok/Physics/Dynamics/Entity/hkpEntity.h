/// @file
/// @brief hkpEntity: a world object the solver moves — material, constraints, the embedded motion, listeners, local frame.
#pragma once

#include <Physics/Dynamics/World/hkpWorldObject.h>
#include <Physics/Dynamics/Common/hkpMaterial.h>
#include <Physics/Dynamics/Motion/Rigid/hkpKeyframedRigidMotion.h>
#include <Common/Base/Container/Array/hkSmallArray.h>
#include <Common/Base/Types/Geometry/LocalFrame/hkLocalFrame.h>
#include <Physics/Dynamics/World/hkpSimulationIsland.h>

namespace CB { namespace schema
{
    class hkpConstraintInstance;
    class hkpContactListener;
    class hkpEntityListener;
    struct hkConstraintInternal;

    /// @brief Whether adding (or changing) an entity wakes it.
    /// @note Values per the Havok SDK.
    enum hkpEntityActivation
    {
        HK_ENTITY_ACTIVATION_DO_NOT_ACTIVATE = 0,
        HK_ENTITY_ACTIVATION_DO_ACTIVATE     = 1,
    };

    /// @note 0x2d0 bytes (schema/physics/hkpEntity.yaml). Slot 6 per the Havok SDK, unconfirmed.
    class hkpEntity : public hkpWorldObject
    {
    public:
        /// @note 0x10 bytes.
        struct SpuCollisionCallback
        {
            void*    m_util;          ///< +0x00 — hkSpuCollisionCallbackUtil*
            hkUint16 m_capacity;      ///< +0x08
            hkUint8  m_eventFilter;   ///< +0x0a
            hkUint8  m_userFilter;    ///< +0x0b
        };

        /// @brief Seeded from Havok's reflection (size 0x10, version 1).
        struct SmallArraySerializeOverrideType
        {
        public:
            void* m_data;   ///< +0x0, not serialized
            hkUint16 m_size;   ///< +0x8
            hkUint16 m_capacityAndFlags;   ///< +0xa
        };

        /// @brief Seeded from Havok's reflection (size 0x20).
        struct ExtendedListeners
        {
        public:
            SmallArraySerializeOverrideType m_activationListeners;   ///< +0x0, not serialized
            SmallArraySerializeOverrideType m_entityListeners;   ///< +0x10, not serialized
        };

        hkpMaterial                            m_material;                               ///< +0xd0
        void*                                  m_limitContactImpulseUtilAndFlag;         ///< +0xe0
        hkReal                                 m_damageMultiplier;                       ///< +0xe8
        void*                                  m_breakableBody;                          ///< +0xf0
        hkUint32                               m_solverData;                             ///< +0xf8
        hkUint16                               m_storageIndex;                           ///< +0xfc
        hkUint16                               m_contactPointCallbackDelay;              ///< +0xfe
        hkSmallArray<hkConstraintInternal>     m_constraintsMaster;                      ///< +0x100
        hkArray<hkpConstraintInstance*>        m_constraintsSlave;                       ///< +0x110
        hkArray<hkUint8>                       m_constraintRuntime;                      ///< +0x120
        hkpSimulationIsland*                   m_simulationIsland;                       ///< +0x130
        hkInt8                                 m_autoRemoveLevel;                        ///< +0x138
        hkUint8                                m_numShapeKeysInContactPointProperties;   ///< +0x139
        hkUint8                                m_responseModifierFlags;                  ///< +0x13a
        hkUint32                               m_uid;                                    ///< +0x13c
        SpuCollisionCallback                   m_spuCollisionCallback;                   ///< +0x140
        hkpMaxSizeMotion                       m_motion;                                 ///< +0x150
        hkSmallArray<hkpContactListener*>      m_contactListeners;                       ///< +0x290
        hkSmallArray<void*>                    m_actions;                                ///< +0x2a0 — hkpAction*
        hkRefPtr<hkLocalFrame>                 m_localFrame;                             ///< +0x2b0
        void*                                  m_extendedListeners;                      ///< +0x2b8 — ExtendedListeners*
        hkUint32                               m_npData;                                 ///< +0x2c0

        virtual void deallocateInternalArrays();   ///< slot 6

        /// @brief Wakes the entity's simulation island.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void activate();
        /// @brief Registers `cl` for this entity's contact points (reusing a null slot, else appending).
        void addContactListener(hkpContactListener* cl);
        /// @brief Unregisters `cl` (its slot is nulled, not removed).
        void removeContactListener(hkpContactListener* cl);
        /// @brief Registers / unregisters `el` for this entity's add, remove, shape, motion-type and delete events.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void addEntityListener(hkpEntityListener* el);
        void removeEntityListener(hkpEntityListener* el);

        HK_FORCE_INLINE hkpMotion*         getMotion() { return &m_motion; }
        HK_FORCE_INLINE const hkpMotion*   getMotion() const { return &m_motion; }
        HK_FORCE_INLINE const hkpMaterial& getMaterial() const { return m_material; }
        HK_FORCE_INLINE hkUint32           getUid() const { return m_uid; }
        HK_FORCE_INLINE hkLocalFrame*      getLocalFrame() const { return m_localFrame; }
        HK_FORCE_INLINE const hkSmallArray<hkpContactListener*>& getContactListeners() const { return m_contactListeners; }
        HK_FORCE_INLINE hkpSimulationIsland* getSimulationIsland() const { return m_simulationIsland; }
    };
} }
