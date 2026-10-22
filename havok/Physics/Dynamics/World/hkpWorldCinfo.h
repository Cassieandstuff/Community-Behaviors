#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>
#include <Common/Base/hkBase.h>
#include <Physics/Collide/Filter/Group/hkpGroupFilter.h>

namespace CB { namespace havok
{
    class hkWorldMemoryAvailableWatchDog;
    class hkpConvexListFilter;

    /// @brief Seeded from Havok's reflection (size 0x100, version 11).
    class hkpWorldCinfo : public hkReferencedObject
    {
    public:
        enum SolverType
        {
            SOLVER_TYPE_INVALID = 0,
            SOLVER_TYPE_2ITERS_SOFT = 1,
            SOLVER_TYPE_2ITERS_MEDIUM = 2,
            SOLVER_TYPE_2ITERS_HARD = 3,
            SOLVER_TYPE_4ITERS_SOFT = 4,
            SOLVER_TYPE_4ITERS_MEDIUM = 5,
            SOLVER_TYPE_4ITERS_HARD = 6,
            SOLVER_TYPE_8ITERS_SOFT = 7,
            SOLVER_TYPE_8ITERS_MEDIUM = 8,
            SOLVER_TYPE_8ITERS_HARD = 9,
            SOLVER_TYPE_MAX_ID = 10,
        };
        enum SimulationType
        {
            SIMULATION_TYPE_INVALID = 0,
            SIMULATION_TYPE_DISCRETE = 1,
            SIMULATION_TYPE_CONTINUOUS = 2,
            SIMULATION_TYPE_MULTITHREADED = 3,
        };
        enum ContactPointGeneration
        {
            CONTACT_POINT_ACCEPT_ALWAYS = 0,
            CONTACT_POINT_REJECT_DUBIOUS = 1,
            CONTACT_POINT_REJECT_MANY = 2,
        };
        enum BroadPhaseBorderBehaviour
        {
            BROADPHASE_BORDER_ASSERT = 0,
            BROADPHASE_BORDER_FIX_ENTITY = 1,
            BROADPHASE_BORDER_REMOVE_ENTITY = 2,
            BROADPHASE_BORDER_DO_NOTHING = 3,
        };
        enum TreeUpdateType
        {
            REBUILD_ACTIVE = 0,
            REBUILD_ALL = 1,
        };
        hkVector4 m_gravity;   ///< +0x10
        hkInt32 m_broadPhaseQuerySize;   ///< +0x20
        hkReal m_contactRestingVelocity;   ///< +0x24
        hkInt8 m_broadPhaseBorderBehaviour;   ///< +0x28
        hkBool m_mtPostponeAndSortBroadPhaseBorderCallbacks;   ///< +0x29
        hkAabb m_broadPhaseWorldAabb;   ///< +0x30
        hkBool m_useKdTree;   ///< +0x50
        hkBool m_useMultipleTree;   ///< +0x51
        hkInt8 m_treeUpdateType;   ///< +0x52
        hkBool m_autoUpdateKdTree;   ///< +0x53
        hkReal m_collisionTolerance;   ///< +0x54
        hkpCollisionFilter* m_collisionFilter;   ///< +0x58
        hkpConvexListFilter* m_convexListFilter;   ///< +0x60
        hkReal m_expectedMaxLinearVelocity;   ///< +0x68
        hkInt32 m_sizeOfToiEventQueue;   ///< +0x6c
        hkReal m_expectedMinPsiDeltaTime;   ///< +0x70
        hkWorldMemoryAvailableWatchDog* m_memoryWatchDog;   ///< +0x78
        hkInt32 m_broadPhaseNumMarkers;   ///< +0x80
        hkInt8 m_contactPointGeneration;   ///< +0x84
        hkBool m_allowToSkipConfirmedCallbacks;   ///< +0x85
        hkBool m_useHybridBroadphase;   ///< +0x86
        hkReal m_solverTau;   ///< +0x88
        hkReal m_solverDamp;   ///< +0x8c
        hkInt32 m_solverIterations;   ///< +0x90
        hkInt32 m_solverMicrosteps;   ///< +0x94
        hkReal m_maxConstraintViolation;   ///< +0x98
        hkBool m_forceCoherentConstraintOrderingInSolver;   ///< +0x9c
        hkReal m_snapCollisionToConvexEdgeThreshold;   ///< +0xa0
        hkReal m_snapCollisionToConcaveEdgeThreshold;   ///< +0xa4
        hkBool m_enableToiWeldRejection;   ///< +0xa8
        hkBool m_enableDeprecatedWelding;   ///< +0xa9
        hkReal m_iterativeLinearCastEarlyOutDistance;   ///< +0xac
        hkInt32 m_iterativeLinearCastMaxIterations;   ///< +0xb0
        hkUint8 m_deactivationNumInactiveFramesSelectFlag0;   ///< +0xb4
        hkUint8 m_deactivationNumInactiveFramesSelectFlag1;   ///< +0xb5
        hkUint8 m_deactivationIntegrateCounter;   ///< +0xb6
        hkBool m_shouldActivateOnRigidBodyTransformChange;   ///< +0xb7
        hkReal m_deactivationReferenceDistance;   ///< +0xb8
        hkReal m_toiCollisionResponseRotateNormal;   ///< +0xbc
        hkInt32 m_maxSectorsPerMidphaseCollideTask;   ///< +0xc0
        hkInt32 m_maxSectorsPerNarrowphaseCollideTask;   ///< +0xc4
        hkBool m_processToisMultithreaded;   ///< +0xc8
        hkInt32 m_maxEntriesPerToiMidphaseCollideTask;   ///< +0xcc
        hkInt32 m_maxEntriesPerToiNarrowphaseCollideTask;   ///< +0xd0
        hkInt32 m_maxNumToiCollisionPairsSinglethreaded;   ///< +0xd4
        hkReal m_numToisTillAllowedPenetrationSimplifiedToi;   ///< +0xd8
        hkReal m_numToisTillAllowedPenetrationToi;   ///< +0xdc
        hkReal m_numToisTillAllowedPenetrationToiHigher;   ///< +0xe0
        hkReal m_numToisTillAllowedPenetrationToiForced;   ///< +0xe4
        hkBool m_enableDeactivation;   ///< +0xe8
        hkInt8 m_simulationType;   ///< +0xe9
        hkBool m_enableSimulationIslands;   ///< +0xea
        hkUint32 m_minDesiredIslandSize;   ///< +0xec
        hkBool m_processActionsInSingleThread;   ///< +0xf0
        hkBool m_allowIntegrationOfIslandsWithoutConstraintsInASeparateJob;   ///< +0xf1
        hkReal m_frameMarkerPsiSnap;   ///< +0xf4
        hkBool m_fireCollisionCallbacks;   ///< +0xf8
    };
} }
