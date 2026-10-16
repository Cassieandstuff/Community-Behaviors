/// @file
/// @brief hkbProjectData: the project-wide settings a character's behaviors share.
#pragma once

#include <Behavior/Project/hkbProjectStringData.h>

namespace CB { namespace schema
{
    /// @note 0x30 bytes.
    struct hkbProjectData : hkReferencedObject
    {
        hkVector4                      m_worldUpWS;    ///< +0x10
        hkRefPtr<hkbProjectStringData> m_stringData;   ///< +0x20
        /// @brief The event mode a transition effect falls back to when its own is EVENT_MODE_DEFAULT
        ///        (hkbTransitionEffect::EventMode); every effect copies it in activate.
        hkInt8                         m_defaultEventMode;   ///< +0x28

        HK_FORCE_INLINE hkbProjectData() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbProjectData(hkFinishLoadedObjectFlag flag);
    };
} }
