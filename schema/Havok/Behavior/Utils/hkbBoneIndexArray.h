/// @file
/// @brief hkbBoneIndexArray: a bindable list of bone indices.
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace schema
{
    class hkbBoneIndexArray : public hkbBindable
    {
    public:
        hkArray<hkInt16> m_boneIndices;   ///< +0x30

        HK_FORCE_INLINE hkbBoneIndexArray() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbBoneIndexArray(hkFinishLoadedObjectFlag flag);
    };
    static_assert(sizeof(hkbBoneIndexArray) == 0x40, "hkbBoneIndexArray size");
} }
