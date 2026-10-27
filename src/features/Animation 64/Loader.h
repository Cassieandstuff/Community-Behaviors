#pragma once

#include <cstdint>

namespace CB::havok {
    struct IAnimationClipLoader;
    struct hkbContext;
}

/// @file
/// @brief The clip loader at 64 bits ([Animation64] bLoader, AE only): AnimationFileManagerSingleton's five
///        IAnimationClipLoader slots, the resolver, triple lookup and binder below them, ported from the exe with the
///        binding index 64-bit throughout. Everything they reach below is already 32-bit or keyed by CRC triple.

namespace CB::feature::widen {

    /// @brief IAnimationClipLoader::loadAnimation with a 64-bit index: loads the animation at `index` (no clip involved).
    /// @return false while it is still loading.
    bool LoadAnimation(havok::IAnimationClipLoader* loader, const havok::hkbContext& context, std::int64_t index);

    /// @brief IAnimationClipLoader::unloadAnimation with a 64-bit index: releases what LoadAnimation loaded.
    void UnloadAnimation(havok::IAnimationClipLoader* loader, const havok::hkbContext& context, std::int64_t index);

}  // namespace CB::feature::widen
