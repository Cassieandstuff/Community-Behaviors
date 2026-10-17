#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x40).
    class hkpSimulation : public hkReferencedObject
    {
    public:
        enum FindContacts
        {
            FIND_CONTACTS_DEFAULT = 0,
            FIND_CONTACTS_EXTRA = 1,
        };
        enum ResetCollisionInformation
        {
            RESET_TOI = 1,
            RESET_TIM = 2,
            RESET_AABB = 4,
            RESET_ALL = 7,
        };
        enum LastProcessingStep
        {
            INTEGRATE = 0,
            COLLIDE = 1,
        };
        hkUint32 m_determinismCheckFrameCounter;   ///< +0x10
        hkpWorld* m_world;   ///< +0x18
        hkUint8 m_lastProcessingStep;   ///< +0x20
        hkReal m_currentTime;   ///< +0x24
        hkReal m_currentPsiTime;   ///< +0x28
        hkReal m_physicsDeltaTime;   ///< +0x2c
        hkReal m_simulateUntilTime;   ///< +0x30
        hkReal m_frameMarkerPsiSnap;   ///< +0x34
        hkUint32 m_previousStepResult;   ///< +0x38
    };
} }
