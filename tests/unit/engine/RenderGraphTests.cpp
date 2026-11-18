#include "EngineTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

using namespace af;
using af::core::FrameCount;
using af::core::TimeTicks;
using af::test::EngineFixture;
using af::test::playThroughEngine;
using Catch::Approx;

namespace
{

constexpr float centrePanMono = 0.70710677f; // constant-power pan, -3 dB

auto fixedBlocks(int size)
{
    return [size] { return size; };
}

/// Same bit patterns, not merely equal values (so -0.0 and 0.0 differ).
bool bitIdentical(const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](float x, float y) {
                          return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
                      });
}

float impulse(int /*channel*/, FrameCount frame)
{
    return frame == 0 ? 1.0f : 0.0f;
}

} // namespace

TEST_CASE("An impulse at timeline position X sounds exactly at sample X", "[engine]")
{
    for (const core::SampleRate rate : {44'100, 48'000, 96'000})
    {
        for (const FrameCount target :
             {FrameCount{0}, FrameCount{1}, FrameCount{12'345}, FrameCount{rate * 3 + 7}})
        {
            DYNAMIC_SECTION(rate << " Hz, frame " << target)
            {
                EngineFixture f;
                const auto asset = f.addAsset(rate, 1, 1000, impulse);
                auto& layer = f.addLayer();
                f.addClip(layer, asset, core::framesToTicks(target, rate), 0, 1000);

                const auto total = target + 2000;
                const auto out = playThroughEngine(f.compile({rate, 256}), total, fixedBlocks(256));
                for (FrameCount i = 0; i < total; ++i)
                {
                    const auto idx = static_cast<std::size_t>(i);
                    const float expected = i == target ? centrePanMono : 0.0f;
                    if (out.left[idx] != expected || out.right[idx] != expected)
                    {
                        FAIL("frame " << i << ": " << out.left[idx] << ", " << out.right[idx]);
                    }
                }
            }
        }
    }
}

TEST_CASE("Positions given in seconds land on the exact frame at every standard rate", "[engine]")
{
    // 1.5 s = 1 058 400 000 ticks: frame 66 150 at 44.1 kHz, 72 000 at 48 kHz, 144 000 at 96 kHz.
    for (const core::SampleRate rate : {44'100, 48'000, 96'000})
    {
        EngineFixture f;
        const auto asset = f.addAsset(rate, 2, 10, impulse);
        auto& layer = f.addLayer();
        f.addClip(layer, asset, TimeTicks{TimeTicks::perSecond * 3 / 2}, 0, 10);
        const FrameCount expectedFrame = rate * 3 / 2;
        const auto out =
            playThroughEngine(f.compile({rate, 512}), expectedFrame + 10, fixedBlocks(512));
        CAPTURE(rate);
        REQUIRE(out.left[static_cast<std::size_t>(expectedFrame)] ==
                1.0f); // stereo: balance keeps unity
        REQUIRE(out.left[static_cast<std::size_t>(expectedFrame - 1)] == 0.0f);
        REQUIRE(out.right[static_cast<std::size_t>(expectedFrame + 1)] == 0.0f);
    }
}

TEST_CASE("Output is bit-identical for any block size", "[engine]")
{
    for (const auto quality : {engine::RenderQuality::asExport, engine::RenderQuality::draft})
    {
        DYNAMIC_SECTION("quality " << static_cast<int>(quality))
        {
            EngineFixture f;
            const auto noise = [](int channel, FrameCount i)
            {
                return static_cast<float>(std::sin(0.013 * static_cast<double>(i) * (channel + 1)) *
                                              0.5 +
                                          std::sin(0.31 * static_cast<double>(i)) * 0.2);
            };
            const auto mono48 = f.addAsset(48'000, 1, 20'000, noise);
            const auto stereo44 = f.addAsset(44'100, 2, 20'000, noise);
            const auto stereo96 = f.addAsset(96'000, 2, 30'000, noise);

            auto& a = f.addLayer();
            a.pan = -0.4;
            a.gainDb = -3.0;
            auto& c1 = f.addClip(a, mono48, TimeTicks{12'345'678}, 100, 15'000);
            c1.fadeIn = {TimeTicks{TimeTicks::perSecond / 20}, model::FadeCurve::exponential};
            c1.fadeOut = {TimeTicks{TimeTicks::perSecond / 30}, model::FadeCurve::sCurve};
            auto& c2 = f.addClip(a, mono48, TimeTicks{TimeTicks::perSecond / 4}, 0, 9'000);
            c2.reverse = true;

            auto& b = f.addLayer();
            b.pan = 0.7;
            auto& c3 = f.addClip(b, stereo44, TimeTicks{777}, 10, 19'000);
            c3.gainDb = 4.0;
            c3.fadeIn = {TimeTicks{TimeTicks::perSecond / 50}, model::FadeCurve::linear};
            auto& c4 = f.addClip(b, stereo96, TimeTicks{TimeTicks::perSecond / 7}, 500, 29'000);
            c4.reverse = true;
            c4.fadeOut = {TimeTicks{TimeTicks::perSecond / 10}, model::FadeCurve::equalPower};
            f.event().masterGainDb = -1.5;

            const engine::RenderSpec spec{48'000, 512, quality};
            constexpr FrameCount total = 40'000;
            const auto reference = playThroughEngine(f.compile(spec), total, fixedBlocks(512));

            std::mt19937 rng{1234};
            std::uniform_int_distribution<int> sizes{1, 512};
            const auto randomBlocks =
                playThroughEngine(f.compile(spec), total, [&] { return sizes(rng); });
            const auto single = playThroughEngine(f.compile(spec), total, fixedBlocks(1));

            REQUIRE(bitIdentical(reference.left, randomBlocks.left));
            REQUIRE(bitIdentical(reference.right, randomBlocks.right));
            REQUIRE(bitIdentical(reference.left, single.left));
            REQUIRE(bitIdentical(reference.right, single.right));

            float peak = 0.0f;
            for (const float s : reference.left)
            {
                peak = std::max(peak, std::abs(s));
            }
            REQUIRE(peak > 0.1f); // the test signal really is there
        }
    }
}

TEST_CASE("Resampling keeps pitch and level", "[engine]")
{
    // A 1 kHz sine at 44.1 kHz played at 48 kHz must still be a 1 kHz sine.
    EngineFixture f;
    const auto sine = [](int, FrameCount i)
    {
        return static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / 44'100.0));
    };
    const auto asset = f.addAsset(44'100, 2, 44'100, sine);
    auto& layer = f.addLayer();
    f.addClip(layer, asset, TimeTicks{}, 0, 44'100);
    const auto out = playThroughEngine(f.compile({48'000, 512}), 48'000, fixedBlocks(512));

    double maxError = 0.0;
    for (FrameCount i = 4'000; i < 44'000; ++i) // away from the clip edges
    {
        const double ideal =
            0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / 48'000.0);
        maxError = std::max(
            maxError, std::abs(static_cast<double>(out.left[static_cast<std::size_t>(i)]) - ideal));
    }
    REQUIRE(maxError < 1e-3); // better than -66 dB relative to full scale
}

TEST_CASE("Downsampling puts an impulse at the right frame", "[engine]")
{
    // An impulse at 96 kHz frame 20 000 (0.2083 s) is centred on 48 kHz frame 10 000.
    EngineFixture f;
    const auto asset =
        f.addAsset(96'000, 2, 40'000, [](int, FrameCount i) { return i == 20'000 ? 1.0f : 0.0f; });
    auto& layer = f.addLayer();
    f.addClip(layer, asset, TimeTicks{}, 0, 40'000);
    const auto out = playThroughEngine(f.compile({48'000, 256}), 20'000, fixedBlocks(256));
    const auto peak = std::max_element(out.left.begin(), out.left.end(),
                                       [](float a, float b) { return std::abs(a) < std::abs(b); });
    REQUIRE(peak - out.left.begin() == 10'000);
    REQUIRE(*peak == Approx(0.5).margin(0.01)); // band-limited: half the energy at half the rate
}

TEST_CASE("Gain, pan, mute and solo follow the signal flow", "[engine]")
{
    const auto dc = [](int, FrameCount) { return 1.0f; };
    const auto renderFirstFrame = [](EngineFixture& f)
    {
        const auto out = playThroughEngine(f.compile({48'000, 64}), 1, fixedBlocks(64));
        return std::pair{out.left[0], out.right[0]};
    };

    SECTION("mono layer pans with constant power")
    {
        EngineFixture f;
        const auto asset = f.addAsset(48'000, 1, 100, dc);
        auto& layer = f.addLayer();
        f.addClip(layer, asset, TimeTicks{}, 0, 100);
        layer.pan = -1.0;
        const auto [l, r] = renderFirstFrame(f);
        REQUIRE(l == Approx(1.0f));
        REQUIRE(r == Approx(0.0f).margin(1e-7));
    }
    SECTION("stereo layer uses balance")
    {
        EngineFixture f;
        const auto asset = f.addAsset(48'000, 2, 100, dc);
        auto& layer = f.addLayer();
        f.addClip(layer, asset, TimeTicks{}, 0, 100);
        layer.pan = 0.5;
        const auto [l, r] = renderFirstFrame(f);
        REQUIRE(l == Approx(0.5f));
        REQUIRE(r == Approx(1.0f));
    }
    SECTION("clip gain, layer gain and master gain multiply")
    {
        EngineFixture f;
        const auto asset = f.addAsset(48'000, 2, 100, dc);
        auto& layer = f.addLayer();
        auto& clip = f.addClip(layer, asset, TimeTicks{}, 0, 100);
        clip.gainDb = -6.0;
        layer.gainDb = -6.0;
        f.event().masterGainDb = 12.0;
        const auto [l, r] = renderFirstFrame(f);
        REQUIRE(l == Approx(1.0f).epsilon(1e-5));
        REQUIRE(r == Approx(1.0f).epsilon(1e-5));
    }
    SECTION("mute silences, solo isolates")
    {
        EngineFixture f;
        const auto asset = f.addAsset(48'000, 2, 100, dc);
        auto& a = f.addLayer();
        f.addClip(a, asset, TimeTicks{}, 0, 100);
        auto& b = f.addLayer();
        f.addClip(b, asset, TimeTicks{}, 0, 100);
        b.gainDb = -6.0206; // 0.5

        REQUIRE(renderFirstFrame(f).first == Approx(1.5f).epsilon(1e-4));
        a.mute = true;
        REQUIRE(renderFirstFrame(f).first == Approx(0.5f).epsilon(1e-4));
        a.mute = false;
        a.solo = true;
        REQUIRE(renderFirstFrame(f).first == Approx(1.0f));
        b.solo = true;
        b.mute = true; // mute wins over solo
        REQUIRE(renderFirstFrame(f).first == Approx(1.0f));
    }
}

TEST_CASE("Reverse plays the source range backwards", "[engine]")
{
    EngineFixture f;
    const auto asset =
        f.addAsset(48'000, 2, 1000, [](int, FrameCount i) { return static_cast<float>(i); });
    auto& layer = f.addLayer();
    auto& clip = f.addClip(layer, asset, TimeTicks{}, 100, 200);
    clip.reverse = true;
    const auto out = playThroughEngine(f.compile({48'000, 64}), 120, fixedBlocks(64));
    REQUIRE(out.left[0] == 199.0f);
    REQUIRE(out.left[99] == 100.0f);
    REQUIRE(out.left[100] == 0.0f); // nothing after the clip
}

TEST_CASE("Fades shape the start and end of a clip", "[engine]")
{
    EngineFixture f;
    const auto asset = f.addAsset(48'000, 2, 1000, [](int, FrameCount) { return 1.0f; });
    auto& layer = f.addLayer();
    auto& clip = f.addClip(layer, asset, TimeTicks{}, 0, 1000);
    clip.fadeIn = {core::framesToTicks(100, 48'000), model::FadeCurve::linear};
    clip.fadeOut = {core::framesToTicks(200, 48'000), model::FadeCurve::linear};
    const auto out = playThroughEngine(f.compile({48'000, 128}), 1000, fixedBlocks(128));
    REQUIRE(out.left[0] == 0.0f);
    REQUIRE(out.left[50] == Approx(0.5f));
    REQUIRE(out.left[100] == 1.0f);
    REQUIRE(out.left[700] == 1.0f);
    REQUIRE(out.left[900] == Approx(0.5f));
    REQUIRE(out.left[999] == Approx(1.0f / 200.0f));
}

TEST_CASE("Clips whose asset is not decoded yet play silence and are reported", "[engine]")
{
    EngineFixture f;
    const auto ready = f.addAsset(48'000, 2, 100, [](int, FrameCount) { return 1.0f; });
    const auto pending = f.addAsset(48'000, 2, 100, [](int, FrameCount) { return 1.0f; });
    f.cache().remove(pending.uuid);
    auto& layer = f.addLayer();
    f.addClip(layer, ready, TimeTicks{}, 0, 100);
    f.addClip(layer, pending, TimeTicks{}, 0, 100);

    auto graph = f.compile({48'000, 64});
    REQUIRE(graph->pendingAssets() == std::vector{pending.uuid});
    REQUIRE(graph->layers()[0].clips.size() == 1);

    const auto referenced = engine::GraphCompiler::referencedAssets(f.project(), f.event().id);
    REQUIRE(referenced.size() == 2);
}

TEST_CASE("Unknown events compile to a silent graph with increasing generations", "[engine]")
{
    EngineFixture f;
    const auto project = f.project();
    const auto a = f.compiler().compile(project, model::EventId::generate(), {48'000, 64});
    const auto b = f.compiler().compile(project, model::EventId::generate(), {48'000, 64});
    REQUIRE(a->layers().empty());
    REQUIRE(b->generation() == a->generation() + 1);
}
