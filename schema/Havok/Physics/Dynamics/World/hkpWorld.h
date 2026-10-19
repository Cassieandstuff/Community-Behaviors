/// @file
/// @brief hkpWorld: what the behavior and animation runtimes call on the physics world.
#pragma once

#include <Physics/Collide/Filter/Group/hkpGroupFilter.h>

namespace CB { namespace schema
{
    class hkpEntity;
    class hkpRigidBody;
    class hkpPhantom;
    class hkpCollidable;
    class hkpConstraintInstance;
    class hkpRayHitCollector;
    class hkpCdPointCollector;
    class hkpWorldPostSimulationListener;
    struct hkpWorldRayCastInput;
    struct hkpLinearCastInput;
    /// @note Opaque (only passed through).
    struct hkpCollisionInput {};
    /// @note Opaque: an hkpCollisionInput whose m_tolerance sits at +0x10.
    struct hkpProcessCollisionInput : hkpCollisionInput {};

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

    /// @note Members per Havok's reflection: pointers it leaves untyped are void*; its hkArray and hkMultiThreadCheck
    ///       members, and the run it does not reflect at +0x2a8, are padding. The methods are hkp code, out of line in
    ///       vanilla; every mutator defers itself into the pending-operation queue while m_criticalOperationsLockCount is
    ///       nonzero.
    class hkpWorld : public hkReferencedObject
    {
    public:
        void*               m_simulation;                                       ///< +0x10 — hkpSimulation*; its m_currentTime (+0x24) is getCurrentTime's
        char                m_pad18[0x20 - 0x18];
        hkVector4           m_gravity;                                          ///< +0x20
        void*               m_fixedIsland;                                      ///< +0x30
        hkpRigidBody*       m_fixedRigidBody;                                   ///< +0x38
        hkUint8             m_pad40[0x70 - 0x40];
        void*               m_maintenanceMgr;                                   ///< +0x70
        void*               m_memoryWatchDog;                                   ///< +0x78
        hkBool              m_assertOnRunningOutOfSolverMemory;                 ///< +0x80
        void*               m_broadPhase;                                       ///< +0x88 — hkpBroadPhase*
        void*               m_kdTreeManager;                                    ///< +0x90
        hkBool              m_autoUpdateTree;                                   ///< +0x98
        void*               m_broadPhaseDispatcher;                             ///< +0xa0
        void*               m_phantomBroadPhaseListener;                        ///< +0xa8
        void*               m_entityEntityBroadPhaseListener;                   ///< +0xb0
        void*               m_broadPhaseBorderListener;                         ///< +0xb8
        void*               m_multithreadedSimulationJobData;                   ///< +0xc0
        hkpProcessCollisionInput* m_collisionInput;                             ///< +0xc8
        hkpCollisionFilter* m_collisionFilter;                                  ///< +0xd0
        void*               m_collisionDispatcher;                              ///< +0xd8
        void*               m_convexListFilter;                                 ///< +0xe0
        void*               m_pendingOperations;                                ///< +0xe8
        hkInt32             m_pendingOperationsCount;                           ///< +0xf0
        hkInt32             m_pendingBodyOperationsCount;                       ///< +0xf4
        hkInt32             m_criticalOperationsLockCount;                      ///< +0xf8
        hkInt32             m_criticalOperationsLockCountForPhantoms;           ///< +0xfc
        hkBool              m_blockExecutingPendingOperations;                  ///< +0x100
        hkBool              m_criticalOperationsAllowed;                        ///< +0x101
        void*               m_pendingOperationQueues;                           ///< +0x108
        hkInt32             m_pendingOperationQueueCount;                       ///< +0x110
        hkUint8             m_pad114[0x120 - 0x114];
        hkBool              m_processActionsInSingleThread;                     ///< +0x120
        hkBool              m_allowIntegrationOfIslandsWithoutConstraintsInASeparateJob; ///< +0x121
        hkUint32            m_minDesiredIslandSize;                             ///< +0x124
        void*               m_modifyConstraintCriticalSection;                  ///< +0x128
        hkInt32             m_isLocked;                                         ///< +0x130
        void*               m_islandDirtyListCriticalSection;                   ///< +0x138
        void*               m_propertyMasterLock;                               ///< +0x140
        hkBool              m_wantSimulationIslands;                            ///< +0x148
        hkBool              m_useHybridBroadphase;                              ///< +0x149
        hkReal              m_snapCollisionToConvexEdgeThreshold;               ///< +0x14c
        hkReal              m_snapCollisionToConcaveEdgeThreshold;              ///< +0x150
        hkBool              m_enableToiWeldRejection;                           ///< +0x154
        hkBool              m_wantDeactivation;                                 ///< +0x155
        hkBool              m_shouldActivateOnRigidBodyTransformChange;         ///< +0x156
        hkReal              m_deactivationReferenceDistance;                    ///< +0x158
        hkReal              m_toiCollisionResponseRotateNormal;                 ///< +0x15c
        hkInt32             m_maxSectorsPerMidphaseCollideTask;                 ///< +0x160
        hkInt32             m_maxSectorsPerNarrowphaseCollideTask;              ///< +0x164
        hkBool              m_processToisMultithreaded;                         ///< +0x168
        hkInt32             m_maxEntriesPerToiMidphaseCollideTask;              ///< +0x16c
        hkInt32             m_maxEntriesPerToiNarrowphaseCollideTask;           ///< +0x170
        hkInt32             m_maxNumToiCollisionPairsSinglethreaded;            ///< +0x174
        hkInt32             m_simulationType;                                   ///< +0x178
        hkReal              m_numToisTillAllowedPenetrationSimplifiedToi;       ///< +0x17c
        hkReal              m_numToisTillAllowedPenetrationToi;                 ///< +0x180
        hkReal              m_numToisTillAllowedPenetrationToiHigher;           ///< +0x184
        hkReal              m_numToisTillAllowedPenetrationToiForced;           ///< +0x188
        hkUint32            m_lastEntityUid;                                    ///< +0x18c
        hkUint32            m_lastIslandUid;                                    ///< +0x190
        hkUint32            m_lastConstraintUid;                                ///< +0x194
        hkUint8             m_pad198[0x288 - 0x198];
        void*               m_violatedConstraintArray;                          ///< +0x288
        void*               m_broadPhaseBorder;                                 ///< +0x290
        void*               m_destructionWorld;                                 ///< +0x298
        void*               m_npWorld;                                          ///< +0x2a0
        hkUint8             m_pad2a8[0x3f0 - 0x2a8];
        hkVector4           m_broadPhaseExtents[2];                             ///< +0x3f0
        hkInt32             m_broadPhaseNumMarkers;                             ///< +0x410
        hkInt32             m_sizeOfToiEventQueue;                              ///< +0x414
        hkInt32             m_broadPhaseQuerySize;                              ///< +0x418
        hkInt32             m_broadPhaseUpdateSize;                             ///< +0x41c
        hkInt8              m_contactPointGeneration;                           ///< +0x420

        HK_FORCE_INLINE const hkVector4& getGravity() const { return m_gravity; }
        HK_FORCE_INLINE const hkpProcessCollisionInput* getCollisionInput() const { return m_collisionInput; }

        /// @brief Defers every mutator into the pending-operation queue until the matching unlock.
        HK_FORCE_INLINE void lockCriticalOperations() { m_criticalOperationsLockCount++; }
        /// @brief Releases lockCriticalOperations; the last release runs what was queued meanwhile (unless blocked).
        HK_FORCE_INLINE void unlockAndAttemptToExecutePendingOperations()
        {
            if (--m_criticalOperationsLockCount == 0 && !m_blockExecutingPendingOperations) {
                if (m_pendingOperationsCount) internal_executePendingOperations();
                if (m_pendingOperationQueueCount == 1 && m_pendingBodyOperationsCount) internal_executePendingBodyOperations();
            }
        }
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void internal_executePendingOperations();
        void internal_executePendingBodyOperations();
        /// @brief Registers / unregisters `worldListener` to run after every simulation step.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void addWorldPostSimulationListener(hkpWorldPostSimulationListener* worldListener);
        void removeWorldPostSimulationListener(hkpWorldPostSimulationListener* worldListener);

        /// @brief Takes / releases the world lock around reading or changing world objects.
        /// @note Names provisional. Skyrim's versions enter/leave a global critical section and count in m_isLocked.
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
        /// @brief Whether the simulation stands at its last step (the current time equals the psi time), so there is
        ///        nothing to interpolate.
        /// @note Name provisional; out of line in vanilla (0x140b3e550). Returns a plain bool (in al).
        bool isSimulationAtPsi() const;
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
        /// @brief Sweeps `collA` along `input`, reporting the hits along the path to `castCollector` and, when given, the
        ///        points already touching at the start to `startCollector`.
        /// @note Out of line in vanilla (0x140b3d090). Parameter list per the Havok SDK, unverified; hkpLinearCastInput
        ///       is not declared yet.
        void linearCast(const hkpCollidable* collA, const hkpLinearCastInput& input, hkpCdPointCollector& castCollector,
                        hkpCdPointCollector* startCollector) const;
    };
} }
