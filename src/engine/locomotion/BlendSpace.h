#pragma once

#include <algorithm>
#include <span>

/// @file
/// @brief The weights of a 2D blend space: where a point sits among samples on a plane, by gradient band interpolation
///        (Johansen, "Automated Semi-Procedural Animation for Character Locomotion", 2009).

namespace CB::engine::locomotion {

    /// @brief A sample's place on the blend space's plane, in whatever units its two axes are in.
    struct BlendSample {
        float x = 0.0f;
        float y = 0.0f;
    };

    /// @brief Fill `weights` with each sample's share at (`x`, `y`).
    /// @param weights as many as `samples`; they sum to 1 unless there are no samples
    /// @note  Why bands rather than Unreal's triangulation: triangles need a table built and kept somewhere, and this needs
    ///        nothing between calls. A point on a sample gives that sample all the weight, and outside the samples the
    ///        nearest still weighs: each sample's band stays at 1/2 or more wherever it is the closest, so the sum is never 0.
    /// @note  Distances are plain Euclidean, so the axes' units shape the bands: a speed axis running to 600 beside a
    ///        direction axis running to 1 makes the speed decide. Scale the coordinates to match.
    /// @note  Samples at the same place don't divide by zero: they ignore each other and share the weight.
    inline void GradientBandWeights(std::span<const BlendSample> samples, float x, float y, std::span<float> weights)
    {
        const std::size_t n = std::min(samples.size(), weights.size());
        float             sum = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            const float px = x - samples[i].x;
            const float py = y - samples[i].y;
            float       w  = 1.0f;
            for (std::size_t j = 0; j < n && w > 0.0f; ++j) {
                if (j == i) continue;
                const float ex    = samples[j].x - samples[i].x;
                const float ey    = samples[j].y - samples[i].y;
                const float length = ex * ex + ey * ey;
                if (length <= 1e-12f) continue;
                w = std::min(w, 1.0f - (px * ex + py * ey) / length);
            }
            weights[i] = std::max(w, 0.0f);
            sum += weights[i];
        }
        if (sum <= 0.0f) return;
        for (std::size_t i = 0; i < n; ++i) weights[i] /= sum;
    }

}  // namespace CB::engine::locomotion
