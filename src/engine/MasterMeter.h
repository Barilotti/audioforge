#pragma once

#include "core/TimeTicks.h"
#include "core/rt/Realtime.h"

#include <array>
#include <atomic>

namespace af::engine
{

/// What the master meter shows: linear values, 1.0 = 0 dBFS.
struct MeterReading
{
    std::array<float, 2> peak{}; ///< Highest absolute sample since the previous read().
    std::array<float, 2> rms{};  ///< RMS over about 300 ms (exponential).
    bool clipped = false;        ///< A sample above 0 dBFS since the last resetClip().
};

/// Master output meter. The audio thread measures every block; the UI reads at screen rate.
/// Peaks are accumulated until read, so no block is missed however rarely the UI reads.
class MasterMeter
{
public:
    static constexpr double rmsTimeSeconds = 0.3;

    /// Message thread, while the audio callback is stopped.
    void prepare(core::SampleRate sampleRate) noexcept;

    void measure(const float* left, const float* right, int frames) noexcept AF_NONBLOCKING;

    /// Message thread: returns and clears the accumulated peaks.
    [[nodiscard]] MeterReading read() noexcept;
    void resetClip() noexcept { clipped_.store(false, std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>, 2> peak_{};
    std::array<std::atomic<float>, 2> rms_{};
    std::atomic<bool> clipped_{false};

    // Audio thread.
    std::array<double, 2> meanSquare_{};
    double coefficient_ = 0.0;
};

} // namespace af::engine
