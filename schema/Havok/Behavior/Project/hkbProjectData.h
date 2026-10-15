/// @file
/// @brief hkbProjectData: the project-wide settings a character's behaviors share.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note Only the members the reimplemented code reads are declared (the reflected m_stringData +0x20 is not).
    struct hkbProjectData : hkReferencedObject
    {
        hkVector4 m_worldUpWS;   ///< +0x10
        char      m_pad20[0x28 - 0x20];
        /// @brief The event mode a transition effect falls back to when its own is EVENT_MODE_DEFAULT
        ///        (hkbTransitionEffect::EventMode); every effect copies it in activate.
        hkInt8    m_defaultEventMode;   ///< +0x28
    };
} }
