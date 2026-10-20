/// @file
/// @brief NiExtraData (Gamebryo's named extra data) and BSBound (a box: center and half extents).
/// @note Not Havok: declared only as far as the character controller's code destroys them.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/NiObject.h>
#include <Physics/Utilities/CharacterControl/Bethesda/BSTEventSource.h>

namespace CB { namespace havok
{
    /// @note 0x18 bytes: the name (a BSFixedString) at +0x10.
    class NiExtraData : public NiObject
    {
    public:
        /// @note Out of line in vanilla (0x140d350b0).
        virtual ~NiExtraData();

        hkUint8 m_pad10[0x18 - 0x10];
    };

    /// @note 0x30 bytes.
    class BSBound : public NiExtraData
    {
    public:
        NiPoint3 m_center;    ///< +0x18
        NiPoint3 m_extents;   ///< +0x24
    };
} }
