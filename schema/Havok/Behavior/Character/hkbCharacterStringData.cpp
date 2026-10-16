/// @file
/// @brief hkbCharacterStringData, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in hkbCharacter.h; its destructor stays implicit (vanilla's out-of-line copy,
///       0x140ac61f0, has no vtable store of its own).
#include <Behavior/Character/hkbCharacter.h>

namespace CB { namespace schema
{
    hkbCharacterStringData::hkbCharacterStringData(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_deformableSkinNames(flag),
        m_rigidSkinNames(flag),
        m_animationNames(flag),
        m_animationFilenames(flag),
        m_characterPropertyNames(flag),
        m_retargetingSkeletonMapperFilenames(flag),
        m_lodNames(flag),
        m_mirroredSyncPointSubstringsA(flag),
        m_mirroredSyncPointSubstringsB(flag),
        m_name(flag),
        m_rigName(flag),
        m_ragdollName(flag),
        m_behaviorFilename(flag)
    {
    }
} }
