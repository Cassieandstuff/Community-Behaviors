/// @file
/// @brief The plain event payloads: a real, an int, a string.
/// @note hkbEventPayload itself is declared in hkbEvent.h (the event types carry it).
#pragma once

#include <Behavior/Event/hkbEvent.h>

namespace CB { namespace havok
{
    /// @note 0x18 bytes.
    class hkbRealEventPayload : public hkbEventPayload
    {
    public:
        hkReal m_data;   ///< +0x10

        HK_FORCE_INLINE hkbRealEventPayload() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbRealEventPayload(hkFinishLoadedObjectFlag flag);
    };

    /// @note 0x18 bytes.
    class hkbIntEventPayload : public hkbEventPayload
    {
    public:
        hkInt32 m_data;   ///< +0x10

        HK_FORCE_INLINE hkbIntEventPayload() {}
        /// @note Out of line for the same reason as hkbRealEventPayload's.
        hkbIntEventPayload(hkFinishLoadedObjectFlag flag);
    };

    /// @note 0x18 bytes. The only payload whose getClassType is its own (the others keep hkReferencedObject's).
    class hkbStringEventPayload : public hkbEventPayload
    {
    public:
        hkStringPtr m_data;   ///< +0x10

        HK_FORCE_INLINE hkbStringEventPayload() {}
        /// @note Out of line for the same reason as hkbRealEventPayload's.
        hkbStringEventPayload(hkFinishLoadedObjectFlag flag);

        /// @note Out of line in vanilla (0x140ac2880); getClassType tail-jumps to it.
        static const hkClass& staticClass();

        virtual const void* getClassType() const;   ///< slot 1
    };
} }
