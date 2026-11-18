#pragma once

#include "core/rt/Realtime.h"

#include <cmath>
#include <numbers>

namespace af::dsp
{

/// Linear gain for a level in dB.
[[nodiscard]] inline float dbToGain(double db) noexcept AF_NONBLOCKING
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

struct StereoGain
{
    float left = 1.0f;
    float right = 1.0f;
};

/// Constant-power pan for a mono source: -3 dB in each channel at the centre.
/// pan: -1 hard left ... +1 hard right.
[[nodiscard]] inline StereoGain constantPowerPan(double pan) noexcept AF_NONBLOCKING
{
    const double angle = (pan + 1.0) * std::numbers::pi / 4.0;
    return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}

/// Balance for a stereo source: the centre leaves both channels unchanged; moving right
/// attenuates the left channel linearly, and vice versa.
[[nodiscard]] inline StereoGain balance(double pan) noexcept AF_NONBLOCKING
{
    return {static_cast<float>(pan > 0.0 ? 1.0 - pan : 1.0),
            static_cast<float>(pan < 0.0 ? 1.0 + pan : 1.0)};
}

} // namespace af::dsp
