#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

/// @file
/// @brief The Advanced Controller's load report (char_load_report): what the controller casts and predicts per frame,
///        the frame time beside it, and the size of its per-controller tables, logged every 30 s and plotted to Tracy
///        every frame.
/// @note  Why: a session's frame rate fell until it was unplayable, and the log had no frame time nor any count that
///        could tell work that grows (a table never pruned, casts multiplying) from work that scales with the crowd
///        (every managed NPC predicts). A line every 30 s tells them apart over a whole session.

namespace CB::feature::controller {

    enum class LoadCounter : std::uint8_t {
        Rays,               ///< floor rays (bhkWorld::PickObject)
        Sweeps,             ///< shape casts (hkpWorld::linearCast)
        PredictPlayer,      ///< trajectory predictions for the player (or the ridden horse)
        PredictNpc,         ///< trajectory predictions for managed NPCs
        PredictNpcSkipped,  ///< NPC steps the distance rate skipped (CharPredictor: fNpcPredictNear, fNpcPredictEvery)
        PredictMicros,      ///< microseconds spent predicting, every character
        kCount
    };

    /// @brief Adds `n` to `counter`. @note Any thread; one relaxed atomic add.
    void CountLoad(LoadCounter counter, std::uint64_t n = 1);

    /// @brief Times a scope into LoadCounter::PredictMicros.
    class LoadTimer {
    public:
        LoadTimer() : m_start(std::chrono::steady_clock::now()) {}
        ~LoadTimer()
        {
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - m_start).count();
            CountLoad(LoadCounter::PredictMicros, static_cast<std::uint64_t>(us > 0 ? us : 0));
        }
        LoadTimer(const LoadTimer&)            = delete;
        LoadTimer& operator=(const LoadTimer&) = delete;

    private:
        std::chrono::steady_clock::time_point m_start;
    };

    /// @return the predictor's per-controller states (CharPredictor.cpp), entries never pruned.
    std::size_t PredictorStateCount();

}  // namespace CB::feature::controller
