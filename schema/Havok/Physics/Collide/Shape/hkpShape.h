/// @file
/// @brief hkpShape: the root of every collision shape.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Addresses a child within a shape hierarchy; HK_INVALID_SHAPE_KEY for none.
    typedef hkUint32 hkpShapeKey;
    enum { HK_INVALID_SHAPE_KEY = 0xffffffff };

    struct hkpShapeContainer;
    struct hkpShapeRayCastInput;
    struct hkpShapeRayCastOutput;

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

        virtual void unk_03() const;   ///< slot 3
        /// @brief The shape's children, null for a shape without any.
        /// @note Name per the Havok SDK.
        virtual const hkpShapeContainer* getContainer() const;   ///< slot 4
        virtual void unk_05() const;   ///< slot 5
        virtual void unk_06() const;   ///< slot 6
        virtual void unk_07() const;   ///< slot 7
        /// @brief Casts `input`'s ray (in the shape's space) into `output`, keeping a nearer hit already there.
        /// @note Name per the Havok SDK; the hkBool comes back through a hidden pointer.
        virtual hkBool castRay(const hkpShapeRayCastInput& input, hkpShapeRayCastOutput& output) const;   ///< slot 8

        HK_FORCE_INLINE hkpShapeType getType() const { return static_cast<hkpShapeType>(m_type); }
        HK_FORCE_INLINE hkUlong  getUserData() const { return m_userData; }
    };
} }
