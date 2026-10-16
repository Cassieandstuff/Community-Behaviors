/// @file
/// @brief hkpShape: the root of every collision shape.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Addresses a child within a shape hierarchy; HK_INVALID_SHAPE_KEY for none.
    typedef hkUint32 hkpShapeKey;
    enum { HK_INVALID_SHAPE_KEY = 0xffffffff };

    /// @brief The shape type tag hkpShape::m_type holds.
    /// @note Values per the Havok SDK's hkpShapeType; HK_SHAPE_CAPSULE (5, the capsule constructor's) and
    ///       HK_SHAPE_CONVEX_TRANSLATE (11, the shape hkbDefaultRaycastInterface::castRay backs off by its radius) are
    ///       confirmed by vanilla, the rest are not.
    enum hkpShapeType
    {
        HK_SHAPE_INVALID           = 0,
        HK_SHAPE_SPHERE            = 1,
        HK_SHAPE_CYLINDER          = 2,
        HK_SHAPE_TRIANGLE          = 3,
        HK_SHAPE_BOX               = 4,
        HK_SHAPE_CAPSULE           = 5,
        HK_SHAPE_CONVEX_VERTICES   = 6,
        HK_SHAPE_COLLECTION        = 7,
        HK_SHAPE_BV_TREE           = 8,
        HK_SHAPE_LIST              = 9,
        HK_SHAPE_MOPP              = 10,
        HK_SHAPE_CONVEX_TRANSLATE  = 11,
        HK_SHAPE_CONVEX_TRANSFORM  = 12,
    };

    /// @note 0x20 bytes (schema/physics/hkpShape.yaml). m_type is an hkpShapeType, stored in 32 bits.
    class hkpShape : public hkReferencedObject
    {
    public:
        hkUlong  m_userData;   ///< +0x10
        hkUint32 m_type;       ///< +0x18

        HK_FORCE_INLINE hkpShapeType getType() const { return static_cast<hkpShapeType>(m_type); }
        HK_FORCE_INLINE hkUlong  getUserData() const { return m_userData; }
    };
} }
