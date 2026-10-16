/// @file
/// @brief hkbEventPayloadList: a payload carrying several payloads.
#pragma once

#include <Behavior/Event/hkbEvent.h>

namespace CB { namespace schema
{
    /// @note 0x20 bytes.
    class hkbEventPayloadList : public hkbEventPayload
    {
    public:
        /// @note Reference-holding raw pointers: the destructor releases each, first to last.
        hkArray<hkbEventPayload*> m_payloads;   ///< +0x10

        HK_FORCE_INLINE hkbEventPayloadList() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbEventPayloadList(hkFinishLoadedObjectFlag flag);
        /// @note Out of line in vanilla (0x140ae5870); the scalar deleting destructor calls it.
        ~hkbEventPayloadList();
    };
} }
