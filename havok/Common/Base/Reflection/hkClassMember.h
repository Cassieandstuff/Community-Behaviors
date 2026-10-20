#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Reflection/hkClass.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkClassEnum;
    class hkCustomAttributes;

    /// @brief Seeded from Havok's reflection (size 0x28).
    class hkClassMember
    {
    public:
        enum Type
        {
            TYPE_VOID = 0,
            TYPE_BOOL = 1,
            TYPE_CHAR = 2,
            TYPE_INT8 = 3,
            TYPE_UINT8 = 4,
            TYPE_INT16 = 5,
            TYPE_UINT16 = 6,
            TYPE_INT32 = 7,
            TYPE_UINT32 = 8,
            TYPE_INT64 = 9,
            TYPE_UINT64 = 10,
            TYPE_REAL = 11,
            TYPE_VECTOR4 = 12,
            TYPE_QUATERNION = 13,
            TYPE_MATRIX3 = 14,
            TYPE_ROTATION = 15,
            TYPE_QSTRANSFORM = 16,
            TYPE_MATRIX4 = 17,
            TYPE_TRANSFORM = 18,
            TYPE_ZERO = 19,
            TYPE_POINTER = 20,
            TYPE_FUNCTIONPOINTER = 21,
            TYPE_ARRAY = 22,
            TYPE_INPLACEARRAY = 23,
            TYPE_ENUM = 24,
            TYPE_STRUCT = 25,
            TYPE_SIMPLEARRAY = 26,
            TYPE_HOMOGENEOUSARRAY = 27,
            TYPE_VARIANT = 28,
            TYPE_CSTRING = 29,
            TYPE_ULONG = 30,
            TYPE_FLAGS = 31,
            TYPE_HALF = 32,
            TYPE_STRINGPTR = 33,
            TYPE_RELARRAY = 34,
            TYPE_MAX = 35,
        };
        enum FlagValues
        {
            FLAGS_NONE = 0,
            ALIGN_8 = 128,
            ALIGN_16 = 256,
            NOT_OWNED = 512,
            SERIALIZE_IGNORED = 1024,
        };
        enum DeprecatedFlagValues
        {
            DEPRECATED_SIZE_8 = 8,
            DEPRECATED_ENUM_8 = 8,
            DEPRECATED_SIZE_16 = 16,
            DEPRECATED_ENUM_16 = 16,
            DEPRECATED_SIZE_32 = 32,
            DEPRECATED_ENUM_32 = 32,
        };
        const char* m_name;   ///< +0x0
        hkClass* m_class;   ///< +0x8
        hkClassEnum* m_enum;   ///< +0x10
        hkUint8 m_type;   ///< +0x18
        hkUint8 m_subtype;   ///< +0x19
        hkInt16 m_cArraySize;   ///< +0x1a
        hkUint16 m_flags;   ///< +0x1c
        hkUint16 m_offset;   ///< +0x1e
        hkCustomAttributes* m_attributes;   ///< +0x20, not serialized
    };
} }
