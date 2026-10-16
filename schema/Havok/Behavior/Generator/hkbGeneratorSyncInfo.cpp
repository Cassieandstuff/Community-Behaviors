/// @file
/// @brief hkbGeneratorSyncInfo, reimplemented against vanilla (AE 1.6.1170) — mirror so far.
#include <Behavior/Generator/hkbGeneratorSyncInfo.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>

namespace CB { namespace schema
{
    void hkbGeneratorSyncInfo::mirror(const hkbContext& context)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        for (int i = 0; i < m_numSyncPoints; i++) {
            m_syncPoints[i].m_id = rootBehavior->getMirroredExternalId(m_syncPoints[i].m_id);
        }
        m_isMirrored = true;
    }
} }
