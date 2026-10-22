/// @file
/// @brief The plain event payloads, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Event/hkbEventPayload.h>

namespace CB { namespace havok
{
    hkbRealEventPayload::hkbRealEventPayload(hkFinishLoadedObjectFlag flag) : hkbEventPayload(flag)
    {
    }

    hkbIntEventPayload::hkbIntEventPayload(hkFinishLoadedObjectFlag flag) : hkbEventPayload(flag)
    {
    }

    hkbStringEventPayload::hkbStringEventPayload(hkFinishLoadedObjectFlag flag) : hkbEventPayload(flag), m_data(flag)
    {
    }

    const void* hkbStringEventPayload::getClassType() const
    {
        return &staticClass();
    }
} }
