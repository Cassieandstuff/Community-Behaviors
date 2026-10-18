/// @file
/// @brief bhkCharProxyControllerCinfo: the construction info of a bhkCharProxyController.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterProxyCinfo.h>

namespace CB { namespace schema
{
    /// @note 0x140 bytes; vtable 0x1419edf80.
    class bhkCharProxyControllerCinfo : public bhkCharacterControllerCinfo
    {
    public:
        bhkCharProxyControllerCinfo();
        /// @note Out of line in vanilla (0x140ed0670), and inlined into the deleting destructor.
        virtual ~bhkCharProxyControllerCinfo();

        virtual void  SetPosition(const hkVector4& position);
        /// @brief The proxy cinfo's keep distance.
        virtual float Unk_02() const;
        /// @brief The proxy cinfo's contact angle sensitivity.
        virtual float Unk_03() const;
        /// @brief Sets the proxy cinfo's contact angle sensitivity.
        virtual void  Unk_04(float value);
        /// @brief 1.
        virtual float Unk_05() const;

        /// @brief What the controller's bhkCharacterProxy is built from (Init fills the shape phantom).
        bhkCharacterProxyCinfo m_proxyCinfo;   ///< +0xb0
    };
} }
