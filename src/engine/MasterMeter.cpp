#include "engine/MasterMeter.h"

#include <algorithm>
#include <cmath>

namespace af::engine
{

namespace
{

void storeMax(std::atomic<float>& target, float value) noexcept
{
    float current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }
}

} // namespace

void MasterMeter::prepare(core::SampleRate sampleRate) noexcept
{
    coefficient_ = sampleRate > 0
                       ? 1.0 - std::exp(-1.0 / (rmsTimeSeconds * static_cast<double>(sampleRate)))
                       : 1.0;
    meanSquare_ = {};
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto index = static_cast<std::size_t>(ch);
        peak_[index].store(0.0f, std::memory_order_relaxed);
        rms_[index].store(0.0f, std::memory_order_relaxed);
    }
}

void MasterMeter::measure(const float* left, const float* right, int frames) noexcept
{
    const std::array<const float*, 2> channels{left, right};
    bool clipped = false;
    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        const float* samples = channels[ch];
        float peak = 0.0f;
        double meanSquare = meanSquare_[ch];
        for (int i = 0; i < frames; ++i)
        {
            const float x = samples[i];
            peak = std::max(peak, std::abs(x));
            const auto xd = static_cast<double>(x);
            meanSquare += coefficient_ * (xd * xd - meanSquare);
        }
        if (meanSquare < 1e-20)
        {
            meanSquare = 0.0; // keep denormals out of the next block
        }
        meanSquare_[ch] = meanSquare;
        clipped = clipped || peak > 1.0f;
        storeMax(peak_[ch], peak);
        rms_[ch].store(static_cast<float>(std::sqrt(meanSquare)), std::memory_order_relaxed);
    }
    if (clipped)
    {
        clipped_.store(true, std::memory_order_relaxed);
    }
}

MeterReading MasterMeter::read() noexcept
{
    MeterReading reading;
    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        reading.peak[ch] = peak_[ch].exchange(0.0f, std::memory_order_relaxed);
        reading.rms[ch] = rms_[ch].load(std::memory_order_relaxed);
    }
    reading.clipped = clipped_.load(std::memory_order_relaxed);
    return reading;
}

} // namespace af::engine
