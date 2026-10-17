/// @file
/// @brief hkClass: a reflected class's description (name, parent, members).
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    class hkClassEnum;
    class hkClassMember;
    class hkCustomAttributes;

    /// @note The exe reflects four descriptions of hkClass itself (two 0x40, two 0x50); this is the 0x50 one, the layout
    ///       the running exe's constructor (0x140a97290) fills: every field below, through m_describedVersion at +0x4c.
    struct hkClass
    {
        enum SignatureFlags { SIGNATURE_LOCAL = 1 };
        enum FlagValues { FLAGS_NONE = 0, FLAGS_NOT_SERIALIZABLE = 1 };

        const char*               m_name;                       ///< +0x00
        const hkClass*            m_parent;                     ///< +0x08
        int                       m_objectSize;                 ///< +0x10
        int                       m_numImplementedInterfaces;   ///< +0x14
        const hkClassEnum*        m_declaredEnums;              ///< +0x18
        int                       m_numDeclaredEnums;           ///< +0x20
        const hkClassMember*      m_declaredMembers;            ///< +0x28
        int                       m_numDeclaredMembers;         ///< +0x30
        const void*               m_defaults;                   ///< +0x38
        const hkCustomAttributes* m_attributes;                 ///< +0x40
        /// @note Havok's hkFlags<FlagValues, hkUint32>; that wrapper is not declared here.
        hkUint32                  m_flags;                      ///< +0x48
        int                       m_describedVersion;           ///< +0x4c

        /// @brief Whether this class is `other` or one of its parents (an `other` object can be used as one of these).
        hkBool isSuperClass(const hkClass& other) const;
        /// @note Out of line in vanilla (0x140a972f0).
        const char* getName() const;
        /// @brief The size of an instance in bytes.
        /// @note Out of line in vanilla (0x140a978e0).
        int getObjectSize() const;
    };
} }
