#pragma once

#include "core/rt/Realtime.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace af::dsp
{

/// Kaiser-windowed sinc interpolation kernel, tabulated for real-time use (architecture v0.2,
/// section 4.5: "Polyphase windowed-sinc"). Built off the audio thread, immutable afterwards.
///
/// `cutoff` is the low-pass corner relative to the source Nyquist frequency: 1 when upsampling,
/// target/source rate when downsampling (so the kernel also band-limits). The kernel spans
/// `radius()` source samples on each side of the interpolation point.
class SincKernel
{
public:
    /// zeroCrossings: sinc lobes per side at cutoff 1 (quality); phases: table points per sample.
    [[nodiscard]] static std::shared_ptr<const SincKernel>
    create(double cutoff, int zeroCrossings = 16, int phases = 512);

    SincKernel(double cutoff, int zeroCrossings, int phases);

    [[nodiscard]] int radius() const noexcept { return radius_; }
    [[nodiscard]] double cutoff() const noexcept { return cutoff_; }

    /// Kernel value at `distance` source samples from the centre (0 outside the radius).
    [[nodiscard]] float weight(double distance) const noexcept AF_NONBLOCKING
    {
        const double x = std::abs(distance) * static_cast<double>(phases_);
        const auto index = static_cast<std::size_t>(x);
        if (index + 1 >= table_.size())
        {
            return 0.0f;
        }
        const auto frac = static_cast<float>(x - static_cast<double>(index));
        return table_[index] + (table_[index + 1] - table_[index]) * frac;
    }

private:
    double cutoff_;
    int radius_;
    int phases_;
    std::vector<float> table_; // h(i / phases) for i in [0, radius * phases], plus one zero
};

} // namespace af::dsp
