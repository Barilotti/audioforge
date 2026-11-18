#pragma once

#include <compare>
#include <cstdint>

namespace af::core
{

/// Sample rate in whole hertz. AudioForge supports 8 kHz ... 768 kHz.
using SampleRate = std::int64_t;

/// Number of audio frames (samples per channel).
using FrameCount = std::int64_t;

/// A position or length on the timeline, in ticks of 1/705 600 000 second (architecture v0.2,
/// section 5.3). The tick rate divides evenly by every standard sample rate from 8 kHz to
/// 352.8 kHz, so timeline positions convert to frames exactly at those rates.
///
/// Positions inside a source file are not ticks: they are FrameCount values at the file's own
/// sample rate, which stay exact for non-standard rates as well.
struct TimeTicks
{
    std::int64_t value = 0;

    static constexpr std::int64_t perSecond = 705'600'000;

    /// Largest magnitude that survives a round trip through an IEEE double (2^53 - 1), which is
    /// what many JSON parsers use. Project files never store values outside this range.
    static constexpr std::int64_t maxJsonSafe = (std::int64_t{1} << 53) - 1;

    /// Maximum length of an Event timeline: 24 hours.
    static constexpr std::int64_t maxEventLength = perSecond * 60 * 60 * 24;

    [[nodiscard]] static constexpr TimeTicks fromSeconds(std::int64_t seconds) noexcept
    {
        return TimeTicks{seconds * perSecond};
    }

    [[nodiscard]] constexpr bool isJsonSafe() const noexcept
    {
        return value >= -maxJsonSafe && value <= maxJsonSafe;
    }

    /// Seconds as floating point. For display only: never feed it back into the timeline.
    [[nodiscard]] constexpr double toSecondsApprox() const noexcept
    {
        return static_cast<double>(value) / static_cast<double>(perSecond);
    }

    constexpr auto operator<=>(const TimeTicks&) const noexcept = default;

    constexpr TimeTicks& operator+=(TimeTicks other) noexcept
    {
        value += other.value;
        return *this;
    }

    constexpr TimeTicks& operator-=(TimeTicks other) noexcept
    {
        value -= other.value;
        return *this;
    }

    [[nodiscard]] friend constexpr TimeTicks operator+(TimeTicks a, TimeTicks b) noexcept
    {
        return TimeTicks{a.value + b.value};
    }

    [[nodiscard]] friend constexpr TimeTicks operator-(TimeTicks a, TimeTicks b) noexcept
    {
        return TimeTicks{a.value - b.value};
    }

    [[nodiscard]] friend constexpr TimeTicks operator-(TimeTicks a) noexcept
    {
        return TimeTicks{-a.value};
    }
};

/// True if the sample rate is in the supported range [8 kHz, 768 kHz].
[[nodiscard]] constexpr bool isSupportedSampleRate(SampleRate rate) noexcept
{
    return rate >= 8'000 && rate <= 768'000;
}

/// True if every tick position maps to a whole frame at this rate (rate divides perSecond).
[[nodiscard]] constexpr bool isTickExact(SampleRate rate) noexcept
{
    return isSupportedSampleRate(rate) && TimeTicks::perSecond % rate == 0;
}

/// Frame index of the sample at or before `ticks` (rounds toward negative infinity).
/// Exact when isTickExact(rate). Requires isSupportedSampleRate(rate).
[[nodiscard]] FrameCount ticksToFramesFloor(TimeTicks ticks, SampleRate rate) noexcept;

/// Nearest frame to `ticks`; ties round toward positive infinity.
/// Requires isSupportedSampleRate(rate).
[[nodiscard]] FrameCount ticksToFramesNearest(TimeTicks ticks, SampleRate rate) noexcept;

/// Nearest tick to the start of frame `frames`; ties round toward positive infinity.
/// Exact (no rounding) when isTickExact(rate). Requires isSupportedSampleRate(rate).
[[nodiscard]] TimeTicks framesToTicks(FrameCount frames, SampleRate rate) noexcept;

} // namespace af::core
