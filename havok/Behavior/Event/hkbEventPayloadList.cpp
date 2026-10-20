/// @file
/// @brief hkbEventPayloadList, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Event/hkbEventPayloadList.h>

namespace CB { namespace havok
{
    hkbEventPayloadList::hkbEventPayloadList(hkFinishLoadedObjectFlag flag) : hkbEventPayload(flag), m_payloads(flag)
    {
    }

    hkbEventPayloadList::~hkbEventPayloadList()
    {
        const int numPayloads = m_payloads.getSize();
        for (int i = 0; i < numPayloads; i++) {
            if (m_payloads[i]) {
                m_payloads[i]->removeReference();
            }
        }
    }
} }
