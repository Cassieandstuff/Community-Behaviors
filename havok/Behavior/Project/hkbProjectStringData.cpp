/// @file
/// @brief hkbProjectStringData, reimplemented against vanilla (AE 1.6.1170).
/// @note The destructor stays implicit: vanilla's out-of-line copy (0x140ac6c90) has no vtable store of its own.
#include <Behavior/Project/hkbProjectStringData.h>

namespace CB { namespace havok
{
    hkbProjectStringData::hkbProjectStringData(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_animationFilenames(flag),
        m_behaviorFilenames(flag),
        m_characterFilenames(flag),
        m_eventNames(flag),
        m_animationPath(flag),
        m_behaviorPath(flag),
        m_characterPath(flag),
        m_fullPathToSource(flag),
        m_rootPath(flag)
    {
    }
} }
