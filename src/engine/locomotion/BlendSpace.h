#pragma once

#include <algorithm>
#include <array>
#include <span>

/// @file
/// @brief The weights of a 2D blend space: where a point sits among samples on a plane. Two ways: a grid of rows, as vanilla's
///        nested blenders, or gradient band interpolation for scattered samples (Johansen, "Automated Semi-Procedural
///        Animation for Character Locomotion", 2009).

namespace CB::engine::locomotion {

    /// @brief A sample's place on the blend space's plane, in whatever units its two axes are in.
    struct BlendSample {
        float x = 0.0f;
        float y = 0.0f;
    };

    inline constexpr int kMaxBlendSamples = 64;   ///< what a blend space weighs: the weights live on the stack

    /// @brief Where a value falls along one sorted axis: `end` has `endWeight` of it, `begin` the rest.
    struct BlendInterval {
        int   begin     = 0;
        int   end       = 0;
        float endWeight = 0.0f;
    };

    /// @brief hkbBlenderGenerator::computeBlendInterval (0x140ae8510, AE 1.6.1170) over positions already sorted.
    /// @note  A cyclic axis doesn't wrap its value: below cycleMin or at cycleMax and above reads as cycleMin. The wrap is
    ///        the interval from the last position through the cycle's ends to the first.
    inline BlendInterval FindBlendInterval(std::span<const float> p, float value, bool cyclic, float cycleMin, float cycleMax)
    {
        const int n = static_cast<int>(p.size());
        if (n == 0) return {};
        const float lo = cyclic ? cycleMin : p[0];
        const float hi = cyclic ? cycleMax : p[n - 1];
        float       v  = lo;
        if (lo < value) v = hi <= value ? (cyclic ? lo : hi) : value;
        int i = 0;
        while (i < n && !(v < p[i])) ++i;
        const float across = (hi - p[n - 1]) + (p[0] - lo);
        if (i == 0) return { n - 1, 0, 1.0f - (p[0] - v) / across };
        if (i < n) return { i - 1, i, (v - p[i - 1]) / (p[i] - p[i - 1]) };
        if (cyclic) return { n - 1, 0, (v - p[n - 1]) / across };
        return { n == 1 ? 0 : n - 2, n - 1, 1.0f };
    }

    /// @brief Fill `weights` as vanilla's nested blenders would: samples sharing an `x` form a row, the rows blend along x,
    ///        and each row blends along y over its own positions, so rows may differ in count and place.
    /// @param cyclic x wraps between cycleMin and cycleMax, as a direction does
    /// @param weights as many as `samples`, at most kMaxBlendSamples of them are read
    /// @note  Exact against nesting, unlike GradientBandWeights: vanilla's 2D blends convert with no change in their weights.
    /// @note  Sorted on every call, since a node keeps nothing between calls.
    inline void GridWeights(std::span<const BlendSample> samples, float x, float y, bool cyclic, float cycleMin, float cycleMax, std::span<float> weights)
    {
        const int n = std::min({ static_cast<int>(samples.size()), static_cast<int>(weights.size()), kMaxBlendSamples });
        std::array<int, kMaxBlendSamples> order;
        for (int i = 0; i < n; ++i) {
            weights[i] = 0.0f;
            int at     = i;
            for (; at > 0; --at) {
                const BlendSample& before = samples[order[at - 1]];
                if (before.x < samples[i].x || (before.x == samples[i].x && before.y <= samples[i].y)) break;
                order[at] = order[at - 1];
            }
            order[at] = i;
        }
        std::array<float, kMaxBlendSamples> rowAt, along;
        std::array<int, kMaxBlendSamples + 1> rowStart;
        int rows = 0;
        for (int i = 0; i < n; ++i) {
            along[i] = samples[order[i]].y;
            if (i == 0 || samples[order[i]].x != rowAt[rows - 1]) {
                rowAt[rows]    = samples[order[i]].x;
                rowStart[rows] = i;
                ++rows;
            }
        }
        rowStart[rows] = n;
        if (rows == 0) return;

        const auto addRow = [&](int row, float share) {
            const int     first = rowStart[row];
            const BlendInterval in = FindBlendInterval(std::span<const float>(along.data() + first, rowStart[row + 1] - first), y, false, 0.0f, 0.0f);
            weights[order[first + in.end]] += share * in.endWeight;
            if (in.begin != in.end) weights[order[first + in.begin]] += share * (1.0f - in.endWeight);
        };
        const BlendInterval across = FindBlendInterval(std::span<const float>(rowAt.data(), rows), x, cyclic, cycleMin, cycleMax);
        addRow(across.end, across.endWeight);
        if (across.begin != across.end) addRow(across.begin, 1.0f - across.endWeight);
    }

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
