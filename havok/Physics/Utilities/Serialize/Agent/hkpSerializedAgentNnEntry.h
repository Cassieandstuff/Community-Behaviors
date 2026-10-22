#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <Physics/Dynamics/Constraint/Contact/hkpSimpleContactConstraintAtom.h>
#include <Physics/Dynamics/Entity/hkpEntity.h>

namespace CB { namespace havok
{
    struct hkpAgent1nSector;
    struct hkpSerializedSubTrack1nInfo;

    /// @brief Seeded from Havok's reflection (size 0x20).
    struct hkpSerializedTrack1nInfo
    {
    public:
        hkArray<hkpAgent1nSector*> m_sectors;   ///< +0x0
        hkArray<hkpSerializedSubTrack1nInfo*> m_subTracks;   ///< +0x10
    };

    /// @brief Seeded from Havok's reflection (size 0x28).
    struct hkpSerializedSubTrack1nInfo : public hkpSerializedTrack1nInfo
    {
    public:
        hkInt32 m_sectorIndex;   ///< +0x20
        hkInt32 m_offsetInSector;   ///< +0x24
    };

    /// @brief Seeded from Havok's reflection (size 0x170).
    class hkpSerializedAgentNnEntry : public hkReferencedObject
    {
    public:
        enum SerializedAgentType
        {
            INVALID_AGENT_TYPE = 0,
            BOX_BOX_AGENT3 = 1,
            CAPSULE_TRIANGLE_AGENT3 = 2,
            PRED_GSK_AGENT3 = 3,
            PRED_GSK_CYLINDER_AGENT3 = 4,
            CONVEX_LIST_AGENT3 = 5,
            LIST_AGENT3 = 6,
            BV_TREE_AGENT3 = 7,
            COLLECTION_COLLECTION_AGENT3 = 8,
            COLLECTION_AGENT3 = 9,
        };
        hkpEntity* m_bodyA;   ///< +0x10
        hkpEntity* m_bodyB;   ///< +0x18
        hkUlong m_bodyAId;   ///< +0x20
        hkUlong m_bodyBId;   ///< +0x28
        hkBool m_useEntityIds;   ///< +0x30
        hkInt8 m_agentType;   ///< +0x31
        hkUint8 m_pad0x32[0xe];
        hkpSimpleContactConstraintAtom m_atom;   ///< +0x40
        hkArray<hkUint8> m_propertiesStream;   ///< +0x70
        hkArray<hkContactPoint> m_contactPoints;   ///< +0x80
        hkArray<hkUint8> m_cpIdMgr;   ///< +0x90
        hkUint8 m_nnEntryData[160];   ///< +0xa0
        hkpSerializedTrack1nInfo m_trackInfo;   ///< +0x140
        hkUint8 m_endianCheckBuffer[4];   ///< +0x160
        hkUint32 m_version;   ///< +0x164
        hkUint8 m_pad0x168[0x8];
    };
} }
