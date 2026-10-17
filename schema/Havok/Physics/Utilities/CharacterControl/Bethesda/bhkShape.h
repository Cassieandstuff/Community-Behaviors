/// @file
/// @brief bhkShape: Bethesda's wrapper of an hkpShape.
/// @note Not matched here: declared only as far as the character controller's code reads it.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkSerializable.h>

namespace CB { namespace schema
{
    class hkpShape;

    class bhkShape : public bhkSerializable
    {
    public:
        /// @brief The wrapped shape.
        HK_FORCE_INLINE hkpShape* GetShape() const { return reinterpret_cast<hkpShape*>(m_referencedObject); }
    };
} }
