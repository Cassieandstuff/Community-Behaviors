/// @file
/// @brief hkbProjectData: the project-wide settings a character's behaviors share.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note Only the members the reimplemented code reads are declared (the reflected m_worldUpWS +0x10 and
    ///       m_stringData +0x20 are not).
    struct hkbProjectData : hkReferencedObject
    {
        char   m_pad10[0x28 - 0x10];
        /// @brief The event mode a transition effect falls back to when its own is EVENT_MODE_DEFAULT
        ///        (hkbTransitionEffect::EventMode); every effect copies it in activate.
        hkInt8 m_defaultEventMode;   ///< +0x28
    };
} }
