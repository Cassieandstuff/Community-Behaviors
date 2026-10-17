/// @file
/// @brief bhkShape: Bethesda's wrapper of an hkpShape.
/// @note Not matched here: declared only as far as the character controller's code reads it.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkSerializable.h>
#include <Physics/Collide/Agent/Collidable/hkpCdBody.h>

namespace CB { namespace schema
{
    class bhkShapeCollection;

    /// @note 0x28 bytes (per CommonLibSSE).
    class bhkShape : public bhkSerializable
    {
    public:
        virtual void Unk_32();   ///< slot 50
        virtual void Unk_33();   ///< slot 51
        /// @brief This shape as a collection of child shapes, null if it is not one.
        /// @note Name provisional.
        virtual bhkShapeCollection* AsShapeCollection();   ///< slot 52
        virtual void Unk_35();   ///< slot 53

        /// @brief The wrapped shape.
        HK_FORCE_INLINE hkpShape* GetShape() const { return reinterpret_cast<hkpShape*>(m_referencedObject); }

        /// @brief The material of the child at `key`: the collection's child material, the shape's own for
        ///        HK_INVALID_SHAPE_KEY or a shape that is no collection.
        /// @note Name per CommonLibSSE; out of line in vanilla (0x140ebd310).
        hkUint32 GetMaterialID(hkpShapeKey key) const;

        /// @brief The shape's own material (a MATERIAL_ID).
        /// @note Name per CommonLibSSE.
        hkUint32 m_materialID;   ///< +0x20
        /// @note Name per CommonLibSSE.
        hkUint32 m_filterInfo;   ///< +0x24
    };

    /// @note 0x28 bytes (per CommonLibSSE).
    class bhkShapeCollection : public bhkShape
    {
    public:
        /// @brief The material of the child at `key` (a valid key).
        /// @note Name provisional.
        virtual hkUint32 GetChildMaterialID(hkpShapeKey key) const = 0;   ///< slot 54
    };

    /// @note 0x30 bytes (per CommonLibSSE).
    class bhkListShape : public bhkShapeCollection
    {
    public:
        /// @brief When set, every child counts as the list's own m_materialID.
        /// @note Name provisional: the character controller's material lookup reads it so.
        bool    m_useListMaterial;   ///< +0x28
        hkUint8 m_pad29[0x30 - 0x29];
    };

    /// @brief The collection wrapper behind `body`'s parent (`body` itself for a root): the wrapper of a list shape,
    ///        or of the shape collection a BV-tree / MOPP shape holds; null otherwise.
    /// @note Name provisional; out of line in vanilla (0x140e8bf80).
    bhkShapeCollection* bhkGetShapeCollection(const hkpCdBody* body);

    /// @brief The game's material predicate (a function pointer the game installs; 0 while it has none): nonzero when
    ///        `materialID` is a stairs material, one a character walks up at any slope.
    /// @note Name provisional; out of line in vanilla (0x140e8e720).
    hkUint8 bhkIsStairsMaterial(hkUint32 materialID);
} }
