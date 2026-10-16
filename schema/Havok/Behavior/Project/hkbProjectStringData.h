/// @file
/// @brief hkbProjectStringData: a project's file lists and paths.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x78 bytes.
    struct hkbProjectStringData : hkReferencedObject
    {
        hkArray<hkStringPtr> m_animationFilenames;   ///< +0x10
        hkArray<hkStringPtr> m_behaviorFilenames;    ///< +0x20
        hkArray<hkStringPtr> m_characterFilenames;   ///< +0x30
        hkArray<hkStringPtr> m_eventNames;           ///< +0x40
        hkStringPtr          m_animationPath;        ///< +0x50
        hkStringPtr          m_behaviorPath;         ///< +0x58
        hkStringPtr          m_characterPath;        ///< +0x60
        hkStringPtr          m_fullPathToSource;     ///< +0x68
        /// @note Not serialized (always null in a packfile).
        hkStringPtr          m_rootPath;             ///< +0x70

        HK_FORCE_INLINE hkbProjectStringData() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbProjectStringData(hkFinishLoadedObjectFlag flag);
    };
} }
