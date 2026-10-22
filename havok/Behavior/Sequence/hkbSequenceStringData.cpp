/// @file
/// @brief hkbSequenceStringData, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in hkbSequence.h; its destructor stays implicit (vanilla's out-of-line copy, 0x140ac7090,
///       has no vtable store of its own).
#include <Behavior/Sequence/hkbSequence.h>

namespace CB { namespace havok
{
    hkbSequenceStringData::hkbSequenceStringData(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_eventNames(flag), m_variableNames(flag)
    {
    }
} }
