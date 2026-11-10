#pragma once

#include <optional>
#include <vector>

/// @file
/// @brief Distance matching: a clip played by distance instead of by the clock. A stop plays the frame whose distance left
///        to its stop equals the character's predicted distance to theirs; a pivot the same to its turn; a start advances
///        by the distance actually moved. Game-free: the pose search node and CBDistanceMatchModifier share it.
/// @note  The method follows Unreal's Animation Locomotion Library (design only, no code): a per-frame distance curve,
///        a binary search over it, and play-rate clamps on advancing. Distances here are along the root's horizontal
///        PATH, not straight-line, so a curving clip's curve stays monotonic.

namespace CB::engine::locomotion {

    /// @brief Which moment of a clip its curve is measured from.
    enum class DistanceMarker {
        Stop,    ///< the frame from which the root stays still (the clip's last when it never does)
        Pivot,   ///< the root's slowest frame inside the clip (where it turns)
        Start,   ///< the first frame: the curve is distance travelled
    };

    /// @brief A clip's distance curve: per frame, the root's path distance from the marker frame, negative before it.
    struct DistanceCurve {
        double             dt = 1.0 / 30.0;
        std::vector<float> value;          ///< non-decreasing, 0 at the marker
        double             markerTime = -1;   ///< seconds; -1 = no curve
        [[nodiscard]] bool Valid() const { return markerTime >= 0 && value.size() >= 2; }
        [[nodiscard]] double Duration() const { return value.empty() ? 0.0 : (value.size() - 1) * dt; }
        /// @return the curve at `time`, interpolated, clamped to the clip
        [[nodiscard]] float At(double time) const;
    };

    /// @brief Game units/s: a root slower than this counts as still, for finding a stop or a pivot.
    inline constexpr float kStillSpeed = 10.0f;

    /// @brief `rootPositions` (frames x (x, y, z)) measured into a curve from `marker`.
    /// @return an invalid curve when there are under two frames
    DistanceCurve BuildDistanceCurve(const std::vector<float>& rootPositions, int frames, double dt, DistanceMarker marker);

    /// @brief The time at which the curve reaches `distance` (its first such time, interpolated).
    /// @return nullopt outside the curve's range: the caller plays on by the clock (a stop never freezes on a prediction
    ///         that ran out)
    std::optional<double> TimeAtDistance(const DistanceCurve& curve, float distance);

    /// @brief `time` advanced until the curve has gained `moved`, its step held to [minRate, maxRate] x `dt`.
    /// @note  Past the curve's end it plays on at the clamped rate; `moved` <= 0 advances at minRate.
    double AdvanceByDistance(const DistanceCurve& curve, double time, float moved, double dt, double minRate, double maxRate);

    /// @brief The play rate that makes a clip moving at `clipSpeed` cover `speed`, held to [minRate, maxRate]; 1 when the
    ///        clip does not move.
    float RateForSpeed(float clipSpeed, float speed, float minRate, float maxRate);

}  // namespace CB::engine::locomotion
