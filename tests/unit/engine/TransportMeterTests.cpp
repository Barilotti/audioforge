#include "EngineTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace af;
using af::core::FrameCount;
using af::core::TimeTicks;
using af::engine::EngineEvent;
using af::engine::TransportCommand;
using af::test::EngineFixture;
using Catch::Approx;

namespace
{

const engine::RenderSpec spec{48'000, 256};

struct Block
{
    std::array<float, 256> left{};
    std::array<float, 256> right{};

    void run(engine::AudioEngine& engine, int frames = 256)
    {
        std::array<float*, 2> channels{left.data(), right.data()};
        engine.process(channels.data(), 2, frames);
    }

    [[nodiscard]] float peak() const
    {
        float p = 0.0f;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            p = std::max({p, std::abs(left[i]), std::abs(right[i])});
        }
        return p;
    }
};

/// A stereo constant `level` of `frames` frames at frame `start`.
std::unique_ptr<engine::RenderGraph> constantGraph(EngineFixture& fixture, float level,
                                                   FrameCount frames, FrameCount start = 0)
{
    const auto asset =
        fixture.addAsset(48'000, 2, frames, [level](int, FrameCount) { return level; });
    auto& layer = fixture.addLayer();
    fixture.addClip(layer, asset, core::framesToTicks(start, 48'000), 0, frames);
    return fixture.compile(spec);
}

} // namespace

TEST_CASE("Transport commands are applied in order before the next block", "[engine][transport]")
{
    EngineFixture fixture;
    engine::AudioEngine engine;
    engine.prepare(48'000, 256);
    engine.publish(constantGraph(fixture, 0.5f, 48'000));

    REQUIRE(engine.transport(TransportCommand::seek(1000)));
    REQUIRE(engine.transport(TransportCommand::play()));
    REQUIRE(engine.status().appliedCommands == 0); // nothing happens until the audio thread runs
    REQUIRE_FALSE(engine.status().playing);

    Block block;
    block.run(engine);
    auto status = engine.status();
    REQUIRE(status.appliedCommands == 2);
    REQUIRE(status.playing);
    REQUIRE(status.position == 1256);
    REQUIRE(block.left[0] == Approx(0.5f));

    REQUIRE(engine.transport(TransportCommand::pause()));
    REQUIRE(engine.transport(TransportCommand::seek(-5))); // clamped to 0
    block.run(engine);
    status = engine.status();
    REQUIRE_FALSE(status.playing);
    REQUIRE(status.position == 0);
    REQUIRE(block.peak() == 0.0f);
}

TEST_CASE("A full command queue refuses commands instead of blocking", "[engine][transport]")
{
    engine::AudioEngine engine;
    engine.prepare(48'000, 256);
    std::size_t accepted = 0;
    while (engine.transport(TransportCommand::seek(static_cast<FrameCount>(accepted))))
    {
        ++accepted;
        REQUIRE(accepted <= engine::AudioEngine::commandQueueSize);
    }
    REQUIRE(accepted >= engine::AudioEngine::commandQueueSize - 1);

    Block block;
    block.run(engine);
    REQUIRE(engine.status().appliedCommands == accepted);
    REQUIRE(engine.status().position == static_cast<FrameCount>(accepted - 1));
    REQUIRE(engine.transport(TransportCommand::play())); // room again
}

TEST_CASE("Playback stops at the end of the graph and reports it", "[engine][transport]")
{
    EngineFixture fixture;
    engine::AudioEngine engine;
    engine.prepare(48'000, 256);
    auto graph = constantGraph(fixture, 0.25f, 1000, 200); // ends at frame 1200
    REQUIRE(graph->endFrame() == 1200);
    engine.publish(std::move(graph));
    REQUIRE(engine.transport(TransportCommand::play()));

    Block block;
    for (int i = 0; i < 4; ++i)
    {
        block.run(engine);
        REQUIRE(engine.status().playing);
    }
    REQUIRE(engine.status().endFrame == 1200);
    block.run(engine); // frames 1024...1279 cross the end
    REQUIRE(block.left[175] == Approx(0.25f));
    REQUIRE(block.left[176] == 0.0f);
    auto status = engine.status();
    REQUIRE_FALSE(status.playing);
    REQUIRE(status.position == 1280);

    EngineEvent event;
    REQUIRE(engine.popEvent(event));
    REQUIRE(event.kind == EngineEvent::Kind::endReached);
    REQUIRE(event.frame == 1280);
    REQUIRE(event.appliedCommands == 1);
    REQUIRE_FALSE(engine.popEvent(event));

    block.run(engine); // stopped: silence, the position stays
    REQUIRE(block.peak() == 0.0f);
    REQUIRE(engine.status().position == 1280);
}

TEST_CASE("The graph end counts clips whose asset is still decoding", "[engine][transport]")
{
    EngineFixture fixture;
    const auto ready = fixture.addAsset(48'000, 1, 4800, [](int, FrameCount) { return 0.1f; });
    auto& layer = fixture.addLayer();
    fixture.addClip(layer, ready, TimeTicks{}, 0, 4800);

    // An asset known to the project (44.1 kHz, 1 s) but not in the cache.
    auto project = fixture.project();
    model::AssetRef missing{
        model::AssetId::generate(), "/not-decoded.wav", {}, {44'100, 2, 44'100}};
    project.assets.push_back(model::makeNode(missing));
    model::Clip clip;
    clip.id = model::ClipId::generate();
    clip.asset = missing.id;
    clip.position = TimeTicks::fromSeconds(2);
    clip.sourceOutFrames = 44'100;
    auto event = *project.events.front();
    auto extra = *event.layers.front();
    extra.id = model::LayerId::generate();
    extra.clips = {model::makeNode(clip)};
    event.layers.push_back(model::makeNode(extra));
    project.events = {model::makeNode(event)};

    const auto graph = fixture.compiler().compile(project, event.id, spec);
    REQUIRE(graph->pendingAssets().size() == 1);
    REQUIRE(graph->endFrame() == 3 * 48'000);
}

TEST_CASE("The master meter accumulates peaks until read and smooths RMS", "[engine][meter]")
{
    engine::MasterMeter meter;
    meter.prepare(48'000);

    std::vector<float> left(480);
    std::vector<float> right(480, 0.0f);
    left[10] = 0.5f;
    meter.measure(left.data(), right.data(), 480);
    left[10] = 0.0f;
    left[20] = -0.25f;
    meter.measure(left.data(), right.data(), 480);

    auto reading = meter.read();
    REQUIRE(reading.peak[0] == 0.5f); // the louder, earlier block is not lost
    REQUIRE(reading.peak[1] == 0.0f);
    REQUIRE_FALSE(reading.clipped);
    REQUIRE(meter.read().peak[0] == 0.0f); // read() clears the peaks

    // A full-scale sine settles at 1/sqrt(2) RMS after a few time constants.
    const auto omega = 2.0 * std::numbers::pi * 1000.0 / 48'000.0;
    for (int block = 0; block < 200; ++block)
    {
        for (int i = 0; i < 480; ++i)
        {
            const auto n = static_cast<double>(block * 480 + i);
            left[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(omega * n));
            right[static_cast<std::size_t>(i)] = 0.0f;
        }
        meter.measure(left.data(), right.data(), 480);
    }
    reading = meter.read();
    REQUIRE(reading.rms[0] == Approx(std::sqrt(0.5)).margin(0.01));
    REQUIRE(reading.rms[1] == 0.0f);
    REQUIRE(reading.peak[0] == Approx(1.0f).margin(0.001));
}

TEST_CASE("The master meter keeps a clip indication until it is reset", "[engine][meter]")
{
    EngineFixture fixture;
    engine::AudioEngine engine;
    engine.prepare(48'000, 256);
    engine.publish(constantGraph(fixture, 1.5f, 48'000));
    REQUIRE(engine.transport(TransportCommand::play()));
    Block block;
    block.run(engine);
    REQUIRE(engine.meter().read().clipped);

    REQUIRE(engine.transport(TransportCommand::pause()));
    block.run(engine);
    REQUIRE(engine.meter().read().clipped); // sticky
    engine.meter().resetClip();
    block.run(engine);
    const auto reading = engine.meter().read();
    REQUIRE_FALSE(reading.clipped);
    REQUIRE(reading.peak[0] == 0.0f);
}
