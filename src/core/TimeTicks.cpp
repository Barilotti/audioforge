#include "core/TimeTicks.h"

#include <cassert>

namespace af::core
{

namespace
{

struct DivMod
{
    std::int64_t quotient;
    std::int64_t remainder; // always in [0, divisor)
};

constexpr DivMod floorDivMod(std::int64_t value, std::int64_t divisor) noexcept
{
    std::int64_t q = value / divisor;
    std::int64_t r = value % divisor;
    if (r < 0)
    {
        --q;
        r += divisor;
    }
    return {q, r};
}

// Converts `value` from units of 1/from to units of 1/to, i.e. value * to / from, without
// 64-bit overflow for |value| up to 2^62: the remainder term stays below from * to, which is
// < 2^49 for the supported rates. `roundNearest` selects nearest (ties up) instead of floor.
std::int64_t rescale(std::int64_t value, std::int64_t from, std::int64_t to,
                     bool roundNearest) noexcept
{
    const auto [whole, rest] = floorDivMod(value, from);
    const std::int64_t scaledRest = rest * to;
    std::int64_t part = scaledRest / from;
    if (roundNearest && (scaledRest % from) * 2 >= from)
    {
        ++part;
    }
    return whole * to + part;
}

} // namespace

FrameCount ticksToFramesFloor(TimeTicks ticks, SampleRate rate) noexcept
{
    assert(isSupportedSampleRate(rate));
    return rescale(ticks.value, TimeTicks::perSecond, rate, false);
}

FrameCount ticksToFramesNearest(TimeTicks ticks, SampleRate rate) noexcept
{
    assert(isSupportedSampleRate(rate));
    return rescale(ticks.value, TimeTicks::perSecond, rate, true);
}

TimeTicks framesToTicks(FrameCount frames, SampleRate rate) noexcept
{
    assert(isSupportedSampleRate(rate));
    return TimeTicks{rescale(frames, rate, TimeTicks::perSecond, true)};
}

} // namespace af::core
