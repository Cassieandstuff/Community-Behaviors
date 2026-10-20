/// @file
/// @brief hkbCharacterSetup, reimplemented against vanilla (AE 1.6.1170) — the destructor and the retargeting lookup so far.
/// @note The class is declared in hkbCharacter.h.
#include <Behavior/Character/hkbCharacter.h>

#include <Common/Base/Container/String/hkString.h>

namespace CB { namespace havok
{
    hkbCharacterSetup::~hkbCharacterSetup()
    {
    }

    /// @brief The retargeting mapper whose source skeleton (skeleton A) is named `rigName` (exact compare), else null.
    const hkaSkeletonMapper* hkbCharacterSetup::getRetargetingSkeletonMapper(const char* rigName)
    {
        for (int i = 0; i < m_retargetingSkeletonMappers.getSize(); i++) {
            hkaSkeletonMapper* mapper = m_retargetingSkeletonMappers[i];
            if (hkString::strCmp(mapper->m_mapping.m_skeletonA->m_name.cString(), rigName) == 0) {
                return mapper;
            }
        }
        return HK_NULL;
    }
} }
