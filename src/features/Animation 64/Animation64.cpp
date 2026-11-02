#include "PCH.h"

#include "features/Animation 64/Animation64.h"

#include <Common/Base/hkBase.h>
#include <Animation/Animation/Animation/hkaAnimationBinding.h>
#include <Animation/Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Animation/Rig/hkaSkeleton.h>
#include <Behavior/Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGenerator.h>
#include <Behavior/Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Behavior/Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Behavior/Utils/hkbBoneIndexArray.h>

/// @file
/// @brief Holds kFields to havok/'s declarations: a row whose width no longer matches fails the build.

namespace CB::feature::widen {

    namespace {


        template <class A>
        struct ElementOf;
        template <class T>
        struct ElementOf<::hkArray<T>> {
            using type = T;
        };

        template <class F>
        struct SecondParameterOf;
        template <class C, class R, class A, class B>
        struct SecondParameterOf<R (C::*)(A, B)> {
            using type = B;
        };

        template <class T>
        constexpr int kBits = static_cast<int>(sizeof(T) * 8);

        using Track = ::hkbGeneratorOutput::TrackHeader;

        static_assert(kBits<decltype(::hkbClipGenerator::m_animationBindingIndex)> == WidthOf("hkbClipGenerator", "m_animationBindingIndex"));
        static_assert(kBits<decltype(::BSSynchronizedClipGenerator::m_sAnimationBindingIndex)> ==
                      WidthOf("BSSynchronizedClipGenerator", "m_sAnimationBindingIndex"));
        static_assert(kBits<ElementOf<decltype(::hkaSkeleton::m_parentIndices)>::type> == WidthOf("hkaSkeleton", "m_parentIndices[]"));
        static_assert(kBits<ElementOf<decltype(::hkaAnimationBinding::m_transformTrackToBoneIndices)>::type> ==
                      WidthOf("hkaAnimationBinding", "m_transformTrackToBoneIndices[]"));
        static_assert(kBits<ElementOf<decltype(::hkaAnimationBinding::m_floatTrackToFloatSlotIndices)>::type> ==
                      WidthOf("hkaAnimationBinding", "m_floatTrackToFloatSlotIndices[]"));
        static_assert(kBits<ElementOf<decltype(::hkbBoneIndexArray::m_boneIndices)>::type> == WidthOf("hkbBoneIndexArray", "m_boneIndices[]"));
        static_assert(kBits<ElementOf<decltype(::hkaSkeletonMapperData::m_unmappedBones)>::type> ==
                      WidthOf("hkaSkeletonMapperData", "m_unmappedBones[]"));
        static_assert(kBits<decltype(Track::m_capacity)> == WidthOf("hkbGeneratorOutput::TrackHeader", "m_capacity"));
        static_assert(kBits<decltype(Track::m_numData)> == WidthOf("hkbGeneratorOutput::TrackHeader", "m_numData"));
        static_assert(kBits<decltype(Track::m_dataOffset)> == WidthOf("hkbGeneratorOutput::TrackHeader", "m_dataOffset"));
        static_assert(kBits<decltype(Track::m_elementSizeBytes)> == WidthOf("hkbGeneratorOutput::TrackHeader", "m_elementSizeBytes"));
        static_assert(kBits<SecondParameterOf<decltype(&::IAnimationClipLoader::loadAnimation)>::type> ==
                      WidthOf("IAnimationClipLoader", "loadAnimation(animationBindingIndex)"));
        static_assert(kBits<SecondParameterOf<decltype(&::IAnimationClipLoader::unloadAnimation)>::type> ==
                      WidthOf("IAnimationClipLoader", "unloadAnimation(animationBindingIndex)"));

    }  // namespace

}  // namespace CB::feature::widen
