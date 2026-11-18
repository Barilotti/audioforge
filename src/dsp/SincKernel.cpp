#include "dsp/SincKernel.h"

#include <algorithm>
#include <numbers>

namespace af::dsp
{

namespace
{

/// Zeroth-order modified Bessel function of the first kind (power series).
double besselI0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    const double quarterX2 = x * x / 4.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= quarterX2 / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < sum * 1e-17)
        {
            break;
        }
    }
    return sum;
}

constexpr double kaiserBeta = 9.0; // about -90 dB side lobes

} // namespace

std::shared_ptr<const SincKernel> SincKernel::create(double cutoff, int zeroCrossings, int phases)
{
    return std::make_shared<const SincKernel>(cutoff, zeroCrossings, phases);
}

SincKernel::SincKernel(double cutoff, int zeroCrossings, int phases)
    : cutoff_(std::clamp(cutoff, 0.01, 1.0))
    , radius_(static_cast<int>(std::ceil(static_cast<double>(zeroCrossings) / cutoff_)))
    , phases_(phases)
{
    const auto points = static_cast<std::size_t>(radius_) * static_cast<std::size_t>(phases_) + 1;
    table_.resize(points + 1, 0.0f); // the extra zero lets weight() interpolate up to the edge
    const double norm = besselI0(kaiserBeta);
    for (std::size_t i = 0; i < points; ++i)
    {
        const double d = static_cast<double>(i) / static_cast<double>(phases_);
        const double x = d * cutoff_;
        const double sinc =
            x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
        const double r = d / static_cast<double>(radius_);
        const double window = r >= 1.0 ? 0.0 : besselI0(kaiserBeta * std::sqrt(1.0 - r * r)) / norm;
        // Without band-limiting the sinc is exactly zero at whole-sample distances.
        const bool zeroCrossing =
            cutoff_ == 1.0 && i != 0 && i % static_cast<std::size_t>(phases_) == 0;
        table_[i] = zeroCrossing ? 0.0f : static_cast<float>(cutoff_ * sinc * window);
    }
}

} // namespace af::dsp
