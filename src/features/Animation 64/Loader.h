#pragma once

#include <cstdint>

class IAnimationClipLoader;
class hkbContext;

/// @file
/// @brief The clip loader at 64 bits ([Animation64] bLoader, AE only): AnimationFileManagerSingleton's five
///        IAnimationClipLoader slots, the resolver, triple lookup and binder below them, ported from the exe with the
///        binding index 64-bit throughout. Everything they reach below is already 32-bit or keyed by CRC triple.

namespace CB::feature::widen {

    /// @brief IAnimationClipLoader::loadAnimation with a 64-bit index: loads the animation at `index` (no clip involved).
    /// @return false while it is still loading.
    bool LoadAnimation(::IAnimationClipLoader* loader, const ::hkbContext& context, std::int64_t index);

    /// @brief What PinAnimation did.
    enum class Pin {
        Bound,     ///< it took one of the index's bind counts: release it with UnloadAnimation
        Pending,   ///< nothing taken: the file is streaming in (or another entry holds the slot); ask again later
        Nothing,   ///< nothing to hold: the index names no file, or the context has no character data
    };

    /// @brief LoadAnimation that says whether it took a reference, for a caller holding animations loaded without a
    ///        clip (CBPoseSearchGenerator's children).
    /// @note  LoadAnimation's true also covers an index with no file and a stream request, neither of which binds, so an
    ///        UnloadAnimation paired with it could release a count some clip holds.
    Pin PinAnimation(::IAnimationClipLoader* loader, const ::hkbContext& context, std::int64_t index);

    /// @brief IAnimationClipLoader::unloadAnimation with a 64-bit index: releases what LoadAnimation loaded.
    void UnloadAnimation(::IAnimationClipLoader* loader, const ::hkbContext& context, std::int64_t index);

}  // namespace CB::feature::widen
