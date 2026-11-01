#pragma once

#include "interface/AnimationDef.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief Motion matching's warm-up cache: measured clips through the schema (CBMotionMatchingCache / CBMotionClip),
///        stored as a packfile, so the plugin skips measuring a clip whose animation unit is unchanged.

namespace CB::engine::anim {

    /// @brief One clip as MeasureClip + AppendClipFeatures + its unit's curves leave it.
    struct MmCachedClip {
        std::string               name;          ///< the animationName, byte-exact
        std::uint64_t             fingerprint = 0;
        int                       frames      = 0;
        float                     frameDuration = 0;
        float                     duration      = 0;
        std::vector<float>        features;      ///< frames × features::kDims, raw
        std::vector<float>        rootPositions; ///< frames × (x, y, z)
        std::vector<AnimCurveDef> curves;
    };

    /// @return the fingerprint of an animation unit's text, as MmCachedClip::fingerprint stores it
    std::uint64_t MmFingerprint(std::string_view unitText) noexcept;

    /// @brief `clips` as the cache packfile.
    /// @return false with `err` set when the schema lacks a cache class.
    bool WriteMmCache(const std::vector<MmCachedClip>& clips, std::vector<std::uint8_t>& out, std::string& err);

    /// @brief The cache packfile `bytes` back as clips.
    /// @return false with `err` set when it does not parse or was measured under another feature layout (the caller
    ///         then measures every clip again).
    bool ReadMmCache(const std::vector<std::uint8_t>& bytes, std::vector<MmCachedClip>& out, std::string& err);

}  // namespace CB::engine::anim
