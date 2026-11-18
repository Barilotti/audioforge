#include "dsp/Fade.h"
#include "dsp/Gain.h"
#include "dsp/SincKernel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace af::dsp;
using Catch::Approx;

TEST_CASE("dB to gain", "[dsp]")
{
    REQUIRE(dbToGain(0.0) == 1.0f);
    REQUIRE(dbToGain(-6.0206) == Approx(0.5f).epsilon(1e-4));
    REQUIRE(dbToGain(20.0) == Approx(10.0f));
}

TEST_CASE("Pan laws", "[dsp]")
{
    const auto centre = constantPowerPan(0.0);
    REQUIRE(centre.left == Approx(0.70710678f));
    REQUIRE(centre.right == Approx(0.70710678f));
    REQUIRE(constantPowerPan(-1.0).right == Approx(0.0f).margin(1e-7));
    REQUIRE(constantPowerPan(1.0).left == Approx(0.0f).margin(1e-7));
    for (double pan = -1.0; pan <= 1.0; pan += 0.125)
    {
        const auto g = constantPowerPan(pan);
        REQUIRE(g.left * g.left + g.right * g.right == Approx(1.0f)); // constant power
    }
    REQUIRE(balance(0.0).left == 1.0f);
    REQUIRE(balance(0.0).right == 1.0f);
    REQUIRE(balance(-0.25).right == 0.75f);
    REQUIRE(balance(1.0).left == 0.0f);
}

TEST_CASE("Fade shapes start at silence, end at unity and rise monotonically", "[dsp]")
{
    for (const auto shape :
         {FadeShape::linear, FadeShape::equalPower, FadeShape::exponential, FadeShape::sCurve})
    {
        REQUIRE(fadeGain(shape, 0.0) == 0.0f);
        REQUIRE(fadeGain(shape, 1.0) == 1.0f);
        float previous = 0.0f;
        for (int i = 1; i <= 100; ++i)
        {
            const float g = fadeGain(shape, i / 100.0);
            REQUIRE(g >= previous);
            previous = g;
        }
    }
    REQUIRE(fadeGain(FadeShape::linear, 0.25) == 0.25f);
    REQUIRE(fadeGain(FadeShape::sCurve, 0.5) == Approx(0.5f));
    const float ep = fadeGain(FadeShape::equalPower, 0.5);
    REQUIRE(ep * ep * 2.0f == Approx(1.0f)); // -3 dB at the midpoint
}

TEST_CASE("Sinc kernel", "[dsp]")
{
    const auto full = SincKernel::create(1.0);
    REQUIRE(full->radius() == 16);
    REQUIRE(full->weight(0.0) == 1.0f);
    REQUIRE(full->weight(1.0) == 0.0f); // zero crossings at whole samples
    REQUIRE(full->weight(-3.0) == 0.0f);
    REQUIRE(full->weight(0.5) == Approx(0.6366).margin(0.01)); // 2/pi, slightly windowed
    REQUIRE(full->weight(16.0) == 0.0f);
    REQUIRE(full->weight(100.0) == 0.0f);

    const auto half = SincKernel::create(0.5);
    REQUIRE(half->radius() == 32);
    REQUIRE(half->weight(0.0) == 0.5f);

    // The weights of any fractional position sum to about the DC gain of 1.
    for (const double frac : {0.0, 0.1, 0.37, 0.5, 0.93})
    {
        double sum = 0.0;
        for (int j = -half->radius(); j <= half->radius(); ++j)
        {
            sum += static_cast<double>(half->weight(frac - j));
        }
        REQUIRE(sum == Approx(1.0).margin(1e-3));
    }
}
