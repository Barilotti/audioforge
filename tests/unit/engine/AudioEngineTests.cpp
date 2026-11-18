#include "EngineTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numbers>

using namespace af;
using af::core::FrameCount;
using af::core::TimeTicks;
using af::test::EngineFixture;
using Catch::Approx;

namespace
{

/// A fixture with one stereo layer playing a constant `level` from frame 0.
struct DcProject
{
    EngineFixture fixture;

    explicit DcProject(float level)
    {
        const auto asset =
            fixture.addAsset(48'000, 2, 96'000, [level](int, FrameCount) { return level; });
        auto& layer = fixture.addLayer();
        fixture.addClip(layer, asset, TimeTicks{}, 0, 96'000);
    }
};

struct Output
{
    std::array<float, 1024> left{};
    std::array<float, 1024> right{};

    void run(engine::AudioEngine& engine, int frames)
    {
        std::array<float*, 2> channels{left.data(), right.data()};
        engine.process(channels.data(), 2, frames);
    }
};

const engine::RenderSpec spec{48'000, 512};

} // namespace

TEST_CASE("The newest published graph wins; older ones never reach the audio thread",
          "[engine][handover]")
{
    DcProject p{1.0f};
    engine::AudioEngine engine;
    engine.prepare(48'000, 512);
    engine.setPlaying(true);

    auto g1 = p.fixture.compile(spec);
    auto g2 = p.fixture.compile(spec);
    auto g3 = p.fixture.compile(spec);
    const auto newest = g3->generation();
    engine.publish(std::move(g1));
    engine.publish(std::move(g2)); // g1 is deleted here, on the publishing thread
    engine.publish(std::move(g3));

    REQUIRE(engine.status().appliedGeneration == 0);
    Output out;
    out.run(engine, 64);
    REQUIRE(engine.status().appliedGeneration == newest);
    REQUIRE(out.left[0] == 1.0f);
    REQUIRE(engine.status().position == 64);
    REQUIRE(engine.collectRetired() == 0); // nothing was retired: g3 was the first current graph
}

TEST_CASE("A full RetireQueue defers the swap instead of freeing on the audio thread",
          "[engine][handover]")
{
    DcProject p{1.0f};
    engine::AudioEngine engine;
    engine.prepare(48'000, 512);
    Output out;

    // The first graph becomes current; each of the next 64 swaps retires one graph.
    std::uint64_t applied = 0;
    for (int i = 0; i < 65; ++i)
    {
        auto graph = p.fixture.compile(spec);
        applied = graph->generation();
        engine.publish(std::move(graph));
        out.run(engine, 16);
        REQUIRE(engine.status().appliedGeneration == applied);
    }
    REQUIRE(engine.status().deferredSwaps == 0);

    auto waiting = p.fixture.compile(spec);
    const auto waitingGeneration = waiting->generation();
    engine.publish(std::move(waiting));
    out.run(engine, 16);
    out.run(engine, 16);
    REQUIRE(engine.status().appliedGeneration == applied); // still the old graph
    REQUIRE(engine.status().deferredSwaps == 2);

    REQUIRE(engine.collectRetired() == 64);
    out.run(engine, 16);
    REQUIRE(engine.status().appliedGeneration == waitingGeneration);
}

TEST_CASE("Graphs compiled for another device format are rejected", "[engine][handover]")
{
    DcProject p{1.0f};
    engine::AudioEngine engine;
    engine.prepare(48'000, 512);
    engine.setPlaying(true);
    Output out;

    engine.publish(p.fixture.compile({44'100, 512}));
    out.run(engine, 32);
    REQUIRE(engine.status().appliedGeneration == 0);
    REQUIRE(engine.status().rejectedGraphs == 1);
    REQUIRE(out.left[0] == 0.0f);

    engine.publish(p.fixture.compile({48'000, 256})); // too small for 512-frame blocks
    out.run(engine, 32);
    REQUIRE(engine.status().rejectedGraphs == 2);
    REQUIRE(engine.collectRetired() == 2);
}

TEST_CASE("Blocks longer than the prepared size are rendered in pieces", "[engine][handover]")
{
    DcProject p{0.5f};
    engine::AudioEngine engine;
    engine.prepare(48'000, 256);
    engine.setPlaying(true);
    engine.publish(p.fixture.compile({48'000, 256}));
    Output out;
    out.run(engine, 1000);
    REQUIRE(out.left[999] == 0.5f);
    REQUIRE(engine.status().position == 1000);
}

TEST_CASE("Stopped transport outputs silence and keeps the position; seek moves it",
          "[engine][handover]")
{
    DcProject p{1.0f};
    engine::AudioEngine engine;
    engine.prepare(48'000, 512);
    engine.publish(p.fixture.compile(spec));
    Output out;
    out.left.fill(9.0f);
    out.run(engine, 128);
    REQUIRE(out.left[0] == 0.0f);
    REQUIRE(engine.status().position == 0);

    engine.seek(1000);
    engine.setPlaying(true);
    out.run(engine, 128);
    REQUIRE(engine.status().position == 1128);
    REQUIRE(out.left[0] == 1.0f);
}

TEST_CASE("Output channel mapping", "[engine][handover]")
{
    EngineFixture f;
    const auto asset =
        f.addAsset(48'000, 2, 100, [](int ch, FrameCount) { return ch == 0 ? 0.25f : 0.75f; });
    auto& layer = f.addLayer();
    f.addClip(layer, asset, TimeTicks{}, 0, 100);
    engine::AudioEngine engine;
    engine.prepare(48'000, 64);
    engine.setPlaying(true);
    engine.publish(f.compile({48'000, 64}));

    std::array<float, 8> mono{};
    std::array<float, 8> a{};
    std::array<float, 8> b{};
    std::array<float, 8> c{};
    c.fill(5.0f);

    std::array<float*, 1> single{mono.data()};
    engine.process(single.data(), 1, 8);
    REQUIRE(mono[0] == 0.5f); // mono sum (L + R) / 2

    std::array<float*, 3> three{a.data(), nullptr, c.data()};
    engine.process(three.data(), 3, 8);
    REQUIRE(a[0] == 0.25f);
    REQUIRE(c[0] == 0.0f); // extra channels are silent
    (void)b;
}

TEST_CASE("Structural swaps crossfade over 256 samples with equal power", "[engine][handover]")
{
    DcProject loud{1.0f};
    DcProject quiet{0.0f};
    engine::AudioEngine engine;
    engine.prepare(48'000, 512);
    engine.setPlaying(true);
    engine.publish(loud.fixture.compile(spec));
    Output out;
    out.run(engine, 64);

    engine.publish(quiet.fixture.compile(spec, {.needsCrossfade = true}));
    out.run(engine, 512);
    for (int i = 0; i < engine::AudioEngine::crossfadeFrames; ++i)
    {
        const double t = (i + 0.5) / engine::AudioEngine::crossfadeFrames;
        REQUIRE(out.left[static_cast<std::size_t>(i)] ==
                Approx(std::cos(t * std::numbers::pi / 2.0)).margin(1e-6));
    }
    REQUIRE(out.left[256] == 0.0f);
    REQUIRE(engine.collectRetired() == 1); // the old graph was retired when the fade ended

    // Without the flag the swap is instant.
    engine.publish(loud.fixture.compile(spec));
    out.run(engine, 64);
    REQUIRE(out.left[0] == 1.0f);
}

TEST_CASE("deviceStopped takes back every graph", "[engine][handover]")
{
    DcProject p{1.0f};
    engine::AudioEngine engine;
    engine.prepare(48'000, 512);
    engine.setPlaying(true);
    Output out;
    engine.publish(p.fixture.compile(spec));
    out.run(engine, 64);
    engine.publish(p.fixture.compile(spec, {.needsCrossfade = true}));
    out.run(engine, 64); // crossfade in progress: two graphs held by the audio thread
    engine.publish(p.fixture.compile(spec)); // and one pending

    engine.deviceStopped();
    REQUIRE(engine.status().appliedGeneration == 0);
    out.run(engine, 64);
    REQUIRE(out.left[0] == 0.0f);
    REQUIRE(engine.collectRetired() == 0);
}
