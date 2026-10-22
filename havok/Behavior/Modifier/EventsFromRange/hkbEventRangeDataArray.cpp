/// @file
/// @brief hkbEventRangeDataArray, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in hkbEventsFromRangeModifier.h; its destructor stays implicit (vanilla's out-of-line
///       copy, 0x140ac67c0, has no vtable store of its own).
#include <Behavior/Modifier/EventsFromRange/hkbEventsFromRangeModifier.h>

namespace CB { namespace havok
{
    hkbEventRangeDataArray::hkbEventRangeDataArray(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag), m_eventData(flag)
    {
    }
} }
