/// @file
/// @brief hkbBoneWeightArray: a bindable per-bone weight table (a blender child's bone weights).
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace schema
{
    /// @note A `class`, as in Havok (the class-key is part of the constructors' mangled names).
    class hkbBoneWeightArray : public hkbBindable
    {
    public:
        /// @note A bone past the end weighs 1.
        hkArray<hkReal> m_boneWeights;   ///< +0x30

        HK_FORCE_INLINE hkbBoneWeightArray() {}
        /// @brief Shares the binding set; copies the weights.
        /// @note Out of line in vanilla (0x140ae6fc0, among hkbBlenderGenerator.cpp's functions): plain `inline`, which
        ///       /Ob1 declines here.
        inline hkbBoneWeightArray(const hkbBoneWeightArray& boneWeights) : hkbBindable(boneWeights)
        {
            m_boneWeights = boneWeights.m_boneWeights;
        }

        static const hkClass& staticClass();

        virtual const void* getClassType() const;   ///< slot 1
    };
    static_assert(sizeof(hkbBoneWeightArray) == 0x40, "hkbBoneWeightArray size");
} }
