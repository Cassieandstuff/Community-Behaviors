/// @file
/// @brief hkpWorld: what the behavior and animation runtimes call on the physics world.
#pragma once

#include <Physics/Collide/Filter/Group/hkpGroupFilter.h>

namespace CB { namespace schema
{
    class hkpEntity;
    class hkpPhantom;
    class hkpCollidable;
    class hkpConstraintInstance;
    class hkpRayHitCollector;
    class hkpCdPointCollector;
    struct hkpWorldRayCastInput;
    struct hkpCollisionInput;
    /// @note Opaque: an hkpCollisionInput whose m_tolerance sits at +0x10.
    struct hkpProcessCollisionInput;

    /// @note Havok SDK enum; only success is named here.
    enum hkpStepResult { HK_STEP_RESULT_SUCCESS = 0 };

    /// @brief How updateCollisionFilterOnEntity treats the entity's existing agents.
    /// @note Values per the Havok SDK; both are confirmed by their callers.
    enum hkpUpdateCollisionFilterOnEntityMode
    {
        HK_UPDATE_FILTER_ON_ENTITY_FULL_CHECK                            = 0,
        HK_UPDATE_FILTER_ON_ENTITY_DISABLE_ENTITY_ENTITY_COLLISIONS_ONLY = 1,
    };
    /// @brief Whether updateCollisionFilterOnEntity re-filters shape collections' children too.
    /// @note Values per the Havok SDK.
    enum hkpUpdateCollectionFilterMode
    {
        HK_UPDATE_COLLECTION_FILTER_IGNORE_SHAPE_COLLECTIONS  = 0,
        HK_UPDATE_COLLECTION_FILTER_PROCESS_SHAPE_COLLECTIONS = 1,
    };

    /// @note Only the members behavior code reads are declared (offsets from vanilla); everything below is hkp code,
    ///       out of line in vanilla. Every mutator defers itself into the pending-operation queue while
    ///       m_criticalOperationsLockCount (+0xf8) is nonzero.
    class hkpWorld : public hkReferencedObject
    {
    public:
        void*               m_simulation;           ///< +0x10 — hkpSimulation*; its m_currentTime (+0x24) is getCurrentTime's
        char                m_pad18[0x20 - 0x18];
        hkVector4           m_gravity;              ///< +0x20
        char                m_pad30[0x88 - 0x30];
        void*               m_broadPhase;           ///< +0x88 — hkpBroadPhase*
        char                m_pad90[0xc8 - 0x90];
        hkpProcessCollisionInput* m_collisionInput; ///< +0xc8
        hkpCollisionFilter* m_collisionFilter;      ///< +0xd0

        HK_FORCE_INLINE const hkVector4& getGravity() const { return m_gravity; }
        HK_FORCE_INLINE const hkpProcessCollisionInput* getCollisionInput() const { return m_collisionInput; }

        /// @brief Takes / releases the world lock around reading or changing world objects.
        /// @note Names provisional. Skyrim's versions enter/leave a global critical section and count in +0x130.
        void lock();
        void unlock();
        /// @brief Re-runs the collision filter over `entity`'s collision pairs after its filter info changed.
        /// @note `updateMode` is an hkpUpdateCollisionFilterOnEntityMode, `updateShapeCollectionFilter` an
        ///       hkpUpdateCollectionFilterMode.
        void updateCollisionFilterOnEntity(hkpEntity* entity, int updateMode, int updateShapeCollectionFilter);
        /// @brief Advances the simulation by `physicsDeltaTime`.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        hkpStepResult stepDeltaTime(hkReal physicsDeltaTime);
        /// @brief The simulation's current time.
        /// @note Returns the float in xmm0.
        hkTime getCurrentTime() const;
        /// @brief Adds `numEntities` entities at once; `initialActivationState` is an hkpEntityActivation.
        void addEntityBatch(hkpEntity* const* entityBatch, int numEntities, int initialActivationState);
        /// @brief Removes `numEntities` entities at once.
        void removeEntityBatch(hkpEntity* const* entityBatch, int numEntities);
        /// @brief Adds the constraint (the world takes a reference); returns it.
        hkpConstraintInstance* addConstraint(hkpConstraintInstance* constraint);
        /// @brief Removes the constraint.
        /// @note The hkBool comes back through memory.
        hkBool removeConstraint(hkpConstraintInstance* constraint);
        /// @brief Adds the phantom (the world takes a reference); returns it.
        hkpPhantom* addPhantom(hkpPhantom* phantom);
        /// @brief Removes the phantom.
        void removePhantom(hkpPhantom* phantom);
        /// @brief Casts `input`'s ray against every collidable, reporting hits to `collector`.
        void castRay(const hkpWorldRayCastInput& input, hkpRayHitCollector& collector) const;
        /// @brief Reports to `collector` the points of every collidable within `input`'s tolerance of `collA`.
        /// @note Vanilla passes m_collisionInput (an hkpProcessCollisionInput, which is an hkpCollisionInput).
        void getClosestPoints(const hkpCollidable* collA, const hkpCollisionInput& input, hkpCdPointCollector& collector);
    };
} }
