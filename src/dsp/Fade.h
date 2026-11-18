#pragma once

#include "core/rt/Realtime.h"

#include <cmath>
#include <numbers>

namespace af::dsp
{

enum class FadeShape
{
    linear,
    equalPower,  ///< Quarter sine: constant power when crossfaded with its mirror.
    exponential, ///< Fast start, slow end (perceptually even for fade-outs).
    sCurve,      ///< Raised cosine: smooth at both ends.
};

/// Gain of a fade-in at progress t in [0, 1] (0 = silence, 1 = full level). A fade-out uses
/// fadeGain(shape, 1 - t).
[[nodiscard]] inline float fadeGain(FadeShape shape, double t) noexcept AF_NONBLOCKING
{
    if (t <= 0.0)
    {
        return 0.0f;
    }
    if (t >= 1.0)
    {
        return 1.0f;
    }
    switch (shape)
    {
    case FadeShape::linear:
        return static_cast<float>(t);
    case FadeShape::equalPower:
        return static_cast<float>(std::sin(t * std::numbers::pi / 2.0));
    case FadeShape::exponential:
    {
        // -60 dB ... 0 dB on an exponential curve, pinned to exact 0 at the start.
        constexpr double floorDb = -60.0;
        const double gain = std::pow(10.0, floorDb * (1.0 - t) / 20.0);
        constexpr double floorGain = 0.001;
        return static_cast<float>((gain - floorGain) / (1.0 - floorGain));
    }
    case FadeShape::sCurve:
        return static_cast<float>(0.5 - 0.5 * std::cos(t * std::numbers::pi));
    }
    return 1.0f;
}

} // namespace af::dsp
