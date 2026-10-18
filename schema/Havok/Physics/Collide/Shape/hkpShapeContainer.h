#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace schema
{
    /// @brief Scratch room a shape container builds a temporary child shape in (getChildShape).
    /// @note 0x200 bytes, 16-aligned, as the Havok SDK's.
    struct __declspec(align(16)) hkpShapeBuffer
    {
        hkUint8 m_data[0x200];
    };

    /// @brief Seeded from Havok's reflection (size 0x8): the vtable alone.
    /// @note Slots per the Havok SDK; only getChildShape (slot 5) is confirmed (the character controller's step test).
    struct hkpShapeContainer
    {
    public:
        virtual ~hkpShapeContainer();                                                         ///< slot 0
        virtual int         getNumChildShapes() const;                                        ///< slot 1
        virtual hkpShapeKey getFirstKey() const;                                              ///< slot 2
        virtual hkpShapeKey getNextKey(hkpShapeKey oldKey) const;                             ///< slot 3
        virtual hkUint32    getCollisionFilterInfo(hkpShapeKey key) const;                    ///< slot 4
        /// @brief The child at `key`, built in `buffer` when the container keeps none (a mesh's triangle).
        virtual const hkpShape* getChildShape(hkpShapeKey key, hkpShapeBuffer& buffer) const;   ///< slot 5
    };

    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkpSingleShapeContainer : public hkpShapeContainer
    {
    public:
        hkpShape* m_childShape;   ///< +0x8
    };
} }
