#pragma once

#include <cstdint>
#include <limits>

namespace CB::havok { class hkbClipGenerator; }

/// @file
/// @brief hkbClipGenerator's binding index at 64 bits: the schema's animationBindingIndex64 (the class's tail padding),
///        with the 16-bit animationBindingIndex kept beside it as a mirror.
/// @note  The mirror is a tripwire. Converted code reads and writes through BindingIndex / SetBindingIndex, which keep
///        the two in step; so a mirror that disagrees with the slot was written by code that has not been converted
///        (BindingIndex logs and adopts it), and any read of it by exe code is a reader not yet converted (the watch,
///        Watch.h, names its address).

namespace CB::feature::widen {

    /// @return what the 16-bit field holds for `wide`: the value itself when it fits. Past it, -1 in a debug build (the
    ///         clip refuses to play, so a reader still on the old field shows) and the nearest bound in a release build
    ///         (it plays, maybe the wrong animation).
    constexpr std::int16_t MirrorOf(std::int64_t wide) noexcept
    {
        constexpr std::int64_t lo = std::numeric_limits<std::int16_t>::min(), hi = std::numeric_limits<std::int16_t>::max();
        if (wide >= lo && wide <= hi) return static_cast<std::int16_t>(wide);
#ifdef NDEBUG
        return static_cast<std::int16_t>(wide > hi ? hi : lo);
#else
        return -1;
#endif
    }

    /// @return the slot's offset in hkbClipGenerator, or -1 until the class is extended (RegisterAll, kDataLoaded).
    int WideBindingOffset();

    /// @return `clip`'s binding index. A mirror that no longer matches the slot (an unconverted writer changed it) is
    ///         logged and adopted, so the game keeps its value while writers remain unconverted.
    /// @note  Before the class is extended there is no slot: the 16-bit field, as vanilla.
    std::int64_t BindingIndex(havok::hkbClipGenerator* clip);

    /// @brief Writes `index` to the slot and MirrorOf(index) to the 16-bit field.
    void SetBindingIndex(havok::hkbClipGenerator* clip, std::int64_t index);

}  // namespace CB::feature::widen
