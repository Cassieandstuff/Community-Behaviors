/// @file
/// @brief hkbBlenderGeneratorChild: one generator a blender mixes, with its blend weight and bone weights.
#pragma once

#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Utils/hkbBoneWeightArray.h>

namespace CB { namespace havok
{
    /// @note Its functions are compiled in hkbBlenderGenerator.cpp: vanilla places them right before the blender's
    ///       (0x140ae6e60–0x140ae6fbf). The destructor is implicit (vanilla's deleting destructor stamps no vtable).
    class hkbBlenderGeneratorChild : public hkbBindable
    {
    public:
        /// @note Holds a reference the constructors take by hand, released by the BLENDER's destructor (not this
        ///       class's).
        hkbGenerator*                m_generator;              ///< +0x30
        hkRefPtr<hkbBoneWeightArray> m_boneWeights;            ///< +0x38
        /// @note A blend weight, or the child's blend parameter value under FLAG_PARAMETRIC_BLEND.
        hkReal                       m_weight;                 ///< +0x40
        hkReal                       m_worldFromModelWeight;   ///< +0x44
        /// @note Unreflected; neither constructor writes it.
        hkUint8                      m_pad48[0x50 - 0x48];

        HK_FORCE_INLINE hkbBlenderGeneratorChild() : m_generator(HK_NULL), m_weight(1.0f), m_worldFromModelWeight(1.0f) {}
        /// @brief The cloneNode copy: shares the generator (one more reference) and the bone weights, unless they are
        ///        bound to variables, in which case the clone gets its own copy.
        hkbBlenderGeneratorChild(const hkbBlenderGeneratorChild& child);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                        ///< slot 1
        /// @brief This child, then its bone weights.
        virtual void        collectBindables(hkbBindableCollector& collector);   ///< slot 3
    };
    static_assert(sizeof(hkbBlenderGeneratorChild) == 0x50, "hkbBlenderGeneratorChild size");
} }
