/// @file
/// @brief hkbCharacterData, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in hkbCharacter.h; its destructor stays implicit (vanilla's out-of-line copy,
///       0x140ae6970, has no vtable store of its own).
#include <Behavior/Character/hkbCharacter.h>

namespace CB { namespace schema
{
    hkbCharacterData::hkbCharacterData(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_characterPropertyInfos(flag),
        m_numBonesPerLod(flag),
        m_characterPropertyValues(flag),
        m_footIkDriverInfo(flag),
        m_handIkDriverInfo(flag),
        m_stringData(flag),
        m_mirroredSkeletonInfo(flag)
    {
    }
} }
