#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x1).
    struct hkArrayTypeAttribute
    {
    public:
        enum ArrayType
        {
            NONE = 0,
            POINTSOUP = 1,
            ENTITIES = 2,
        };
        hkInt8 m_type;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x8).
    struct hkDataObjectTypeAttribute
    {
    public:
        const char* m_typeName;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x8).
    struct hkDescriptionAttribute
    {
    public:
        const char* m_string;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x8).
    struct hkDocumentationAttribute
    {
    public:
        const char* m_docsSectionTag;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x18).
    struct hkGizmoAttribute
    {
    public:
        enum GizmoType
        {
            POINT = 0,
            SPHERE = 1,
            PLANE = 2,
            ARROW = 3,
        };
        hkBool m_visible;   ///< +0x0
        const char* m_label;   ///< +0x8
        hkInt8 m_type;   ///< +0x10
    };

    /// @brief Seeded from Havok's reflection (size 0x1).
    struct hkLinkAttribute
    {
    public:
        enum Link
        {
            NONE = 0,
            DIRECT_LINK = 1,
            CHILD = 2,
            MESH = 3,
            PARENT_NAME = 4,
        };
        hkInt8 m_type;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x1).
    struct hkMemoryTrackerAttribute
    {
    public:
        hkUint8 m_pad0x0[0x1];
    };

    /// @brief Seeded from Havok's reflection (size 0x1).
    struct hkModelerNodeTypeAttribute
    {
    public:
        enum ModelerType
        {
            DEFAULT = 0,
            LOCATOR = 1,
        };
        hkInt8 m_type;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x8).
    struct hkPostFinishAttribute
    {
    public:
        void* m_postFinishFunction;   ///< +0x0, not serialized
    };

    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkRangeInt32Attribute
    {
    public:
        hkInt32 m_absmin;   ///< +0x0
        hkInt32 m_absmax;   ///< +0x4
        hkInt32 m_softmin;   ///< +0x8
        hkInt32 m_softmax;   ///< +0xc
    };

    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkRangeRealAttribute
    {
    public:
        hkReal m_absmin;   ///< +0x0
        hkReal m_absmax;   ///< +0x4
        hkReal m_softmin;   ///< +0x8
        hkReal m_softmax;   ///< +0xc
    };

    /// @brief Seeded from Havok's reflection (size 0x8).
    struct hkReflectedFileAttribute
    {
    public:
        const char* m_value;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x1).
    struct hkSemanticsAttribute
    {
    public:
        enum Semantics
        {
            UNKNOWN = 0,
            DISTANCE = 1,
            ANGLE = 2,
            NORMAL = 3,
            POSITION = 4,
            COSINE_ANGLE = 5,
        };
        hkInt8 m_type;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x28, version 2).
    struct hkUiAttribute
    {
    public:
        enum HideInModeler
        {
            NONE = 0,
            MAX = 1,
            MAYA = 2,
        };
        hkBool m_visible;   ///< +0x0
        hkInt8 m_hideInModeler;   ///< +0x1
        const char* m_label;   ///< +0x8
        const char* m_group;   ///< +0x10
        const char* m_hideBaseClassMembers;   ///< +0x18
        hkBool m_endGroup;   ///< +0x20
        hkBool m_endGroup2;   ///< +0x21
        hkBool m_advanced;   ///< +0x22
    };
} }
