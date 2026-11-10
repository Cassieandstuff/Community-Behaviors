#include <locomotion/DistanceMatch.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace CB::engine::locomotion {

    float DistanceCurve::At(double time) const
    {
        if (value.empty()) return 0.0f;
        const double f = std::clamp(time / dt, 0.0, static_cast<double>(value.size() - 1));
        const auto   i = static_cast<std::size_t>(f);
        if (i + 1 >= value.size()) return value.back();
        const float u = static_cast<float>(f - static_cast<double>(i));
        return value[i] + (value[i + 1] - value[i]) * u;
    }

    DistanceCurve BuildDistanceCurve(const std::vector<float>& p, int frames, double dt, DistanceMarker marker)
    {
        DistanceCurve out;
        out.dt = dt;
        if (frames < 2 || dt <= 0.0 || p.size() < static_cast<std::size_t>(frames) * 3) return out;
        std::vector<double> path(static_cast<std::size_t>(frames), 0.0);
        std::vector<double> speed(static_cast<std::size_t>(frames), 0.0);
        for (int f = 1; f < frames; ++f) {
            const double dx = p[f * 3] - p[(f - 1) * 3], dy = p[f * 3 + 1] - p[(f - 1) * 3 + 1];
            const double step = std::sqrt(dx * dx + dy * dy);
            path[f]           = path[f - 1] + step;
            speed[f]          = step / dt;
        }
        speed[0] = speed[1];

        int at = 0;
        switch (marker) {
        case DistanceMarker::Start: at = 0; break;
        case DistanceMarker::Stop: {
            /// @note The first frame after which the root never moves again, once it has moved at all.
            at = frames - 1;
            for (int f = frames - 1; f > 0 && speed[f] < kStillSpeed; --f) at = f;
            bool moved = false;
            for (int f = 0; f < at; ++f) moved = moved || speed[f] >= kStillSpeed;
            if (!moved) at = frames - 1;
            break;
        }
        case DistanceMarker::Pivot: {
            /// @note The slowest frame away from the ends (a turn's first and last tenth are its run in and out).
            const int lo = (std::max)(1, frames / 10), hi = (std::min)(frames - 2, frames - 1 - frames / 10);
            at           = lo;
            for (int f = lo; f <= hi; ++f)
                if (speed[f] < speed[at]) at = f;
            break;
        }
        }
        out.value.resize(static_cast<std::size_t>(frames));
        for (int f = 0; f < frames; ++f) out.value[f] = static_cast<float>(path[f] - path[at]);
        out.markerTime = at * dt;
        return out;
    }

    std::optional<double> TimeAtDistance(const DistanceCurve& c, float distance)
    {
        if (!c.Valid() || distance < c.value.front() || distance > c.value.back()) return std::nullopt;
        const auto it = std::lower_bound(c.value.begin(), c.value.end(), distance);
        const auto i  = static_cast<std::size_t>(it - c.value.begin());
        if (i == 0) return 0.0;
        const float a = c.value[i - 1], b = c.value[i];
        const double u = b > a ? (distance - a) / (b - a) : 0.0;
        return ((i - 1) + u) * c.dt;
    }

    double AdvanceByDistance(const DistanceCurve& c, double time, float moved, double dt, double minRate, double maxRate)
    {
        const double lo = minRate * dt, hi = maxRate * dt;
        if (!c.Valid() || dt <= 0.0) return time + dt;
        double step = lo;
        if (moved > 0.0f) {
            if (const auto t = TimeAtDistance(c, c.At(time) + moved)) step = *t - time;
            else step = hi;
        }
        return time + std::clamp(step, lo, hi);
    }

    float RateForSpeed(float clipSpeed, float speed, float minRate, float maxRate)
    {
        if (clipSpeed <= kStillSpeed) return 1.0f;
        return std::clamp(speed / clipSpeed, minRate, maxRate);
    }

}  // namespace CB::engine::locomotion
