/// @file
/// @brief The named event payloads: a name, alone or with a real, an int or a string.
#pragma once

#include <Behavior/Event/hkbEvent.h>

namespace CB { namespace schema
{
    /// @note 0x18 bytes.
    class hkbNamedEventPayload : public hkbEventPayload
    {
    public:
        hkStringPtr m_name;   ///< +0x10

        HK_FORCE_INLINE hkbNamedEventPayload() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbNamedEventPayload(hkFinishLoadedObjectFlag flag);
    };

    /// @note 0x20 bytes.
    class hkbNamedRealEventPayload : public hkbNamedEventPayload
    {
    public:
        hkReal m_data;   ///< +0x18

        HK_FORCE_INLINE hkbNamedRealEventPayload() {}
        /// @note Out of line for the same reason as hkbNamedEventPayload's.
        hkbNamedRealEventPayload(hkFinishLoadedObjectFlag flag);
    };

    /// @note 0x20 bytes.
    class hkbNamedIntEventPayload : public hkbNamedEventPayload
    {
    public:
        hkInt32 m_data;   ///< +0x18

        HK_FORCE_INLINE hkbNamedIntEventPayload() {}
        /// @note Out of line for the same reason as hkbNamedEventPayload's.
        hkbNamedIntEventPayload(hkFinishLoadedObjectFlag flag);
    };

    /// @note 0x20 bytes.
    class hkbNamedStringEventPayload : public hkbNamedEventPayload
    {
    public:
        hkStringPtr m_data;   ///< +0x18

        HK_FORCE_INLINE hkbNamedStringEventPayload() {}
        /// @note Out of line for the same reason as hkbNamedEventPayload's.
        hkbNamedStringEventPayload(hkFinishLoadedObjectFlag flag);
    };
} }
