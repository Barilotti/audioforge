#include "core/TimeTicks.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <random>

using af::core::framesToTicks;
using af::core::isTickExact;
using af::core::SampleRate;
using af::core::ticksToFramesFloor;
using af::core::ticksToFramesNearest;
using af::core::TimeTicks;

TEST_CASE("Tick rate divides every standard sample rate", "[core][time]")
{
    const SampleRate rate = GENERATE(8'000, 11'025, 16'000, 22'050, 32'000, 44'100, 48'000, 64'000,
                                     88'200, 96'000, 176'400, 192'000, 352'800);
    CAPTURE(rate);
    REQUIRE(isTickExact(rate));
}

TEST_CASE("Non-standard rates are reported as not tick-exact", "[core][time]")
{
    const SampleRate rate = GENERATE(37'800, 44'056, 47'952, 384'000, 768'000);
    CAPTURE(rate);
    REQUIRE_FALSE(isTickExact(rate));
}

TEST_CASE("Frames round-trip exactly through ticks at standard rates", "[core][time]")
{
    const SampleRate rate = GENERATE(44'100, 48'000, 96'000, 192'000);
    CAPTURE(rate);

    std::mt19937_64 rng{12345};
    std::uniform_int_distribution<std::int64_t> frames{-rate * 86'400, rate * 86'400};
    for (int i = 0; i < 10'000; ++i)
    {
        const auto f = frames(rng);
        const auto ticks = framesToTicks(f, rate);
        REQUIRE(ticks.value % (TimeTicks::perSecond / rate) == 0);
        REQUIRE(ticksToFramesFloor(ticks, rate) == f);
        REQUIRE(ticksToFramesNearest(ticks, rate) == f);
    }
}

TEST_CASE("One second is exactly rate frames", "[core][time]")
{
    const SampleRate rate = GENERATE(8'000, 44'100, 48'000, 37'800, 384'000);
    CAPTURE(rate);
    REQUIRE(ticksToFramesFloor(TimeTicks::fromSeconds(1), rate) == rate);
    REQUIRE(framesToTicks(rate, rate) == TimeTicks::fromSeconds(1));
}

TEST_CASE("Floor and nearest rounding at non-exact rates", "[core][time]")
{
    // At 384 kHz one frame is 1837.5 ticks.
    constexpr SampleRate rate = 384'000;
    REQUIRE(framesToTicks(1, rate).value == 1838); // ties round up
    REQUIRE(ticksToFramesFloor(TimeTicks{1837}, rate) == 0);
    REQUIRE(ticksToFramesFloor(TimeTicks{1838}, rate) == 1);
    REQUIRE(ticksToFramesNearest(TimeTicks{918}, rate) == 0);
    REQUIRE(ticksToFramesNearest(TimeTicks{919}, rate) == 1);
}

TEST_CASE("Negative positions round toward negative infinity", "[core][time]")
{
    constexpr SampleRate rate = 48'000; // 14 700 ticks per frame
    REQUIRE(ticksToFramesFloor(TimeTicks{-1}, rate) == -1);
    REQUIRE(ticksToFramesFloor(TimeTicks{-14'700}, rate) == -1);
    REQUIRE(ticksToFramesFloor(TimeTicks{-14'701}, rate) == -2);
    REQUIRE(ticksToFramesNearest(TimeTicks{-7'349}, rate) == 0);
    REQUIRE(ticksToFramesNearest(TimeTicks{-7'350}, rate) == 0); // tie rounds up
    REQUIRE(ticksToFramesNearest(TimeTicks{-7'351}, rate) == -1);
}

TEST_CASE("Conversions do not overflow at the JSON-safe limit", "[core][time]")
{
    const SampleRate rate = GENERATE(8'000, 48'000, 384'000, 768'000);
    CAPTURE(rate);
    const TimeTicks limit{TimeTicks::maxJsonSafe};
    const auto frames = ticksToFramesFloor(limit, rate);
    // 2^53 ticks is about 12 765 305 seconds.
    REQUIRE(frames / rate == 12'765'305);
    REQUIRE(ticksToFramesFloor(-limit, rate) == -frames - 1);
}

TEST_CASE("JSON-safe range and event length limit", "[core][time]")
{
    REQUIRE(TimeTicks{TimeTicks::maxJsonSafe}.isJsonSafe());
    REQUIRE(TimeTicks{-TimeTicks::maxJsonSafe}.isJsonSafe());
    REQUIRE_FALSE(TimeTicks{TimeTicks::maxJsonSafe + 1}.isJsonSafe());
    REQUIRE_FALSE(TimeTicks{-TimeTicks::maxJsonSafe - 1}.isJsonSafe());

    REQUIRE(TimeTicks::maxEventLength == TimeTicks::fromSeconds(86'400).value);
    // The 24-hour event limit leaves a large margin below the JSON-safe limit.
    REQUIRE(TimeTicks::maxEventLength * 100 < TimeTicks::maxJsonSafe);
}

TEST_CASE("TimeTicks arithmetic and ordering", "[core][time]")
{
    TimeTicks a{100};
    const TimeTicks b{40};
    REQUIRE((a + b).value == 140);
    REQUIRE((a - b).value == 60);
    REQUIRE((-b).value == -40);
    a += b;
    REQUIRE(a.value == 140);
    a -= b;
    REQUIRE(a.value == 100);
    REQUIRE(b < a);
    REQUIRE(TimeTicks::fromSeconds(2).toSecondsApprox() == 2.0);
}
