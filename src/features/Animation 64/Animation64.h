#pragma once

#include <cstdint>
#include <string_view>

/// @file
/// @brief Animation 64: the 16-bit counts and indices that cap Skyrim's animation data and behavior runtime, to be
///        widened to 64 bits. Nothing is widened yet; this is the list, checked against havok/'s declarations.
/// @note  Owning a class's vtable (platform/classes, [HavokRuntime] sOwnTypes) routes its virtual calls through CB,
///        but every slot still runs code compiled against vanilla's offsets. A field widens only once
///        (1) every function that touches it, virtual or not, is CB's code built against the new layout,
///        (2) every exe function outside that code which touches it is a seam, and
///        (3) for a serialized field, CB writes the packfiles in the new layout and registers the class description
///            for it: packfiles load in place, so the file's layout is the object's.

namespace CB::feature::widen {

    /// @brief Where a field's width is fixed.
    enum class Kind : std::uint8_t {
        Serialized,   ///< a packfile member: the file's layout
        Runtime,      ///< a runtime structure no file holds
        Parameter,    ///< a function parameter: the ABI of every implementation, the exe's included
    };

    /// @brief One field or parameter Animation 64 widens.
    struct WideField {
        std::string_view owner;    ///< the class, struct or interface, as havok/ declares it
        std::string_view member;   ///< the member, or `function(parameter)`
        int              bits;     ///< its width today
        Kind             kind;
    };

    inline constexpr WideField kFields[] = {
        { "hkbClipGenerator", "m_animationBindingIndex", 16, Kind::Serialized },
        { "BSSynchronizedClipGenerator", "m_sAnimationBindingIndex", 16, Kind::Serialized },
        { "hkaSkeleton", "m_parentIndices[]", 16, Kind::Serialized },
        { "hkaAnimationBinding", "m_transformTrackToBoneIndices[]", 16, Kind::Serialized },
        { "hkaAnimationBinding", "m_floatTrackToFloatSlotIndices[]", 16, Kind::Serialized },
        { "hkbBoneIndexArray", "m_boneIndices[]", 16, Kind::Serialized },
        { "hkaSkeletonMapperData", "m_unmappedBones[]", 16, Kind::Serialized },
        { "hkbGeneratorOutput::TrackHeader", "m_capacity", 16, Kind::Runtime },
        { "hkbGeneratorOutput::TrackHeader", "m_numData", 16, Kind::Runtime },
        { "hkbGeneratorOutput::TrackHeader", "m_dataOffset", 16, Kind::Runtime },
        { "hkbGeneratorOutput::TrackHeader", "m_elementSizeBytes", 16, Kind::Runtime },
        { "IAnimationClipLoader", "loadAnimation(animationBindingIndex)", 16, Kind::Parameter },
        { "IAnimationClipLoader", "unloadAnimation(animationBindingIndex)", 16, Kind::Parameter },
    };

    /// @return kFields' width for `owner` and `member`, or 0 when it has no such row.
    constexpr int WidthOf(std::string_view owner, std::string_view member)
    {
        for (const auto& f : kFields)
            if (f.owner == owner && f.member == member) return f.bits;
        return 0;
    }

}  // namespace CB::feature::widen
