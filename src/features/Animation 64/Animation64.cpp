#include "PCH.h"

#include "features/Animation 64/Animation64.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/classes/TypeRegistries.h>

#include <Animation/Animation/hkaAnimationBinding.h>
#include <Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGenerator.h>
#include <Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Utils/hkbBoneIndexArray.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

/// @file
/// @brief Holds kFields to havok/'s declarations (a row whose width no longer matches fails the build), and keeps the
///        clip's 64-bit binding index slot (schema: hkbClipGenerator.animationBindingIndex64) in step with its objects.

namespace CB::feature::widen {

    namespace {

        namespace hk = CB::havok;

        template <class A>
        struct ElementOf;
        template <class T>
        struct ElementOf<hk::hkArray<T>> {
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

        using Track = hk::hkbGeneratorOutput::TrackHeader;

        static_assert(kBits<decltype(hk::hkbClipGenerator::m_animationBindingIndex)> == WidthOf("hkbClipGenerator", "m_animationBindingIndex"));
        static_assert(kBits<decltype(hk::BSSynchronizedClipGenerator::m_sAnimationBindingIndex)> ==
                      WidthOf("BSSynchronizedClipGenerator", "m_sAnimationBindingIndex"));
        static_assert(kBits<ElementOf<decltype(hk::hkaSkeleton::m_parentIndices)>::type> == WidthOf("hkaSkeleton", "m_parentIndices[]"));
        static_assert(kBits<ElementOf<decltype(hk::hkaAnimationBinding::m_transformTrackToBoneIndices)>::type> ==
                      WidthOf("hkaAnimationBinding", "m_transformTrackToBoneIndices[]"));
        static_assert(kBits<ElementOf<decltype(hk::hkaAnimationBinding::m_floatTrackToFloatSlotIndices)>::type> ==
                      WidthOf("hkaAnimationBinding", "m_floatTrackToFloatSlotIndices[]"));
        static_assert(kBits<ElementOf<decltype(hk::hkbBoneIndexArray::m_boneIndices)>::type> == WidthOf("hkbBoneIndexArray", "m_boneIndices[]"));
        static_assert(kBits<ElementOf<decltype(hk::hkaSkeletonMapperData::m_unmappedBones)>::type> ==
                      WidthOf("hkaSkeletonMapperData", "m_unmappedBones[]"));
        static_assert(kBits<decltype(Track::m_capacity)> == WidthOf("hkbGeneratorOutput::TrackHeader", "m_capacity"));
        static_assert(kBits<decltype(Track::m_numData)> == WidthOf("hkbGeneratorOutput::TrackHeader", "m_numData"));
        static_assert(kBits<decltype(Track::m_dataOffset)> == WidthOf("hkbGeneratorOutput::TrackHeader", "m_dataOffset"));
        static_assert(kBits<decltype(Track::m_elementSizeBytes)> == WidthOf("hkbGeneratorOutput::TrackHeader", "m_elementSizeBytes"));
        static_assert(kBits<SecondParameterOf<decltype(&hk::IAnimationClipLoader::loadAnimation)>::type> ==
                      WidthOf("IAnimationClipLoader", "loadAnimation(animationBindingIndex)"));
        static_assert(kBits<SecondParameterOf<decltype(&hk::IAnimationClipLoader::unloadAnimation)>::type> ==
                      WidthOf("IAnimationClipLoader", "unloadAnimation(animationBindingIndex)"));

        /// @brief The 64-bit slot's offset, from the extended class description (the schema's `cb: true` member).
        /// @note  Cached once found; until RegisterAll extends the class (or when it could not), -1 and nothing to keep.
        int WideBindingOffset()
        {
            static std::atomic<int> cached{ -1 };
            int offset = cached.load(std::memory_order_relaxed);
            if (offset < 0) {
                offset = platform::classes::MemberOffset("hkbClipGenerator", "animationBindingIndex64");
                if (offset >= 0) cached.store(offset, std::memory_order_relaxed);
            }
            return offset;
        }

        /// @brief A loaded clip's slot starts as its 16-bit index (the packfile holds -1 there; the slot is zero); a
        ///        clone takes its original's.
        /// @note  Vanilla's copy constructor builds the clone member by member, so the padding the slot lives in is
        ///        whatever the allocator left: it is copied here, never read before.
        void KeepWideBindingIndex(void* object, const void* source)
        {
            const int offset = WideBindingOffset();
            if (offset < 0) return;
            auto* wide = reinterpret_cast<std::int64_t*>(static_cast<std::byte*>(object) + offset);
            if (source) *wide = *reinterpret_cast<const std::int64_t*>(static_cast<const std::byte*>(source) + offset);
            else *wide = static_cast<const hk::hkbClipGenerator*>(object)->m_animationBindingIndex;
        }

        [[maybe_unused]] const bool s_keepWide = (platform::classes::OnOwnedObject("hkbClipGenerator", &KeepWideBindingIndex), true);

    }  // namespace

}  // namespace CB::feature::widen
