/// @file
/// @brief The named event payloads, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Event/hkbNamedEventPayload.h>

namespace CB { namespace havok
{
    hkbNamedEventPayload::hkbNamedEventPayload(hkFinishLoadedObjectFlag flag) : hkbEventPayload(flag), m_name(flag)
    {
    }

    hkbNamedRealEventPayload::hkbNamedRealEventPayload(hkFinishLoadedObjectFlag flag) : hkbNamedEventPayload(flag)
    {
    }

    hkbNamedIntEventPayload::hkbNamedIntEventPayload(hkFinishLoadedObjectFlag flag) : hkbNamedEventPayload(flag)
    {
    }

    hkbNamedStringEventPayload::hkbNamedStringEventPayload(hkFinishLoadedObjectFlag flag) : hkbNamedEventPayload(flag), m_data(flag)
    {
    }
} }
