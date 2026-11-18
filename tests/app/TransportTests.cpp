#include "app/Transport.h"
#include "engine/AudioEngine.h"
#include "engine/GraphCompiler.h"
#include "model/Node.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

using namespace af;
using af::core::FrameCount;
using af::engine::TransportCommand;

namespace
{

/// An engine without a device: tests run its blocks by hand.
struct Rig
{
    assets::AudioBufferCache cache{options()};
    engine::GraphCompiler compiler{cache};
    engine::AudioEngine engine;
    app::Transport transport{engine};

    explicit Rig(FrameCount lengthFrames = 48'000)
    {
        engine.prepare(48'000, 256);
        auto audio = std::make_shared<assets::DecodedAudio>(48'000, 1, lengthFrames);
        std::fill_n(audio->writableChannel(0), lengthFrames, 0.5f);
        model::AssetRef asset{model::AssetId::generate(), "/dc.wav", {}, {48'000, 1, lengthFrames}};
        (void)cache.insertDecoded(asset.id.uuid, audio);
        model::Clip clip;
        clip.id = model::ClipId::generate();
        clip.asset = asset.id;
        clip.sourceOutFrames = lengthFrames;
        model::Layer layer;
        layer.id = model::LayerId::generate();
        layer.clips.push_back(model::makeNode(clip));
        model::Event event;
        event.id = model::EventId::generate();
        event.layers.push_back(model::makeNode(layer));
        model::Project project;
        project.assets.push_back(model::makeNode(asset));
        project.events.push_back(model::makeNode(event));
        engine.publish(compiler.compile(project, event.id, {48'000, 256}));
    }

    void blocks(int count)
    {
        std::array<float, 256> left{};
        std::array<float, 256> right{};
        std::array<float*, 2> channels{left.data(), right.data()};
        for (int i = 0; i < count; ++i)
        {
            engine.process(channels.data(), 2, 256);
        }
        transport.pump();
    }

    static assets::AudioBufferCache::Options options()
    {
        assets::AudioBufferCache::Options o;
        o.budget = assets::MemoryBudget::defaultsFor(4 * assets::gibibyte);
        o.workerThreads = 1;
        return o;
    }
};

} // namespace

TEST_CASE("Stop returns to where playback started, and to zero when already stopped",
          "[app][transport]")
{
    Rig rig;
    rig.transport.seek(1000);
    REQUIRE(rig.transport.position() == 1000); // shown before the audio thread applies it
    rig.transport.play();
    REQUIRE(rig.transport.isPlaying());
    rig.blocks(4);
    REQUIRE(rig.engine.status().playing);
    REQUIRE(rig.transport.position() == 1000 + 4 * 256);

    rig.transport.stop();
    REQUIRE_FALSE(rig.transport.isPlaying());
    REQUIRE(rig.transport.position() == 1000);
    rig.blocks(1);
    REQUIRE_FALSE(rig.engine.status().playing);
    REQUIRE(rig.engine.status().position == 1000);

    rig.transport.stop();
    rig.blocks(1);
    REQUIRE(rig.transport.position() == 0);
}

TEST_CASE("Pause keeps the position; Space toggles play and stop", "[app][transport]")
{
    Rig rig;
    rig.transport.togglePlayPause();
    rig.blocks(2);
    rig.transport.togglePlayPause();
    rig.blocks(1);
    REQUIRE_FALSE(rig.transport.isPlaying());
    REQUIRE(rig.transport.position() == 512);

    rig.transport.togglePlayStop(); // play from 512
    rig.blocks(2);
    REQUIRE(rig.transport.position() == 1024);
    rig.transport.togglePlayStop(); // stop: back to 512
    rig.blocks(1);
    REQUIRE(rig.transport.position() == 512);
}

TEST_CASE("Reaching the end stops and returns to the start position; Play then starts over",
          "[app][transport]")
{
    Rig rig{1000};
    rig.transport.seek(100);
    rig.transport.play();
    rig.blocks(4); // passes frame 1000
    REQUIRE_FALSE(rig.transport.isPlaying());
    REQUIRE(rig.transport.position() == 100);
    rig.blocks(1);
    REQUIRE(rig.engine.status().position == 100);

    rig.transport.seek(5000); // beyond the end
    rig.transport.play();
    rig.blocks(1);
    REQUIRE(rig.transport.isPlaying());
    REQUIRE(rig.engine.status().position == 256); // started over from 0
}

TEST_CASE("An end reported before newer commands is ignored", "[app][transport]")
{
    Rig rig{512};
    rig.transport.play();
    std::array<float, 256> left{};
    std::array<float, 256> right{};
    std::array<float*, 2> channels{left.data(), right.data()};
    rig.engine.process(channels.data(), 2, 256);
    rig.engine.process(channels.data(), 2, 256); // end reached; the event waits in the queue

    rig.transport.pump(); // the user pressed nothing yet: the end is handled
    REQUIRE_FALSE(rig.transport.isPlaying());

    rig.transport.play(); // from 0 again
    rig.engine.process(channels.data(), 2, 256);
    rig.engine.process(channels.data(), 2, 256); // a second end
    rig.transport.seek(100);                     // newer than that end
    rig.transport.pump();
    REQUIRE(rig.transport.isPlaying()); // the stale end did not stop the desired state
}

TEST_CASE("Commands that do not fit the engine queue are kept and collapse", "[app][transport]")
{
    Rig rig;
    // Fill the engine queue directly, as if the audio thread had stalled.
    while (rig.engine.transport(TransportCommand::seek(7)))
    {
    }
    for (int i = 0; i < 10'000; ++i) // far more than the queue holds
    {
        rig.transport.seek(i);
        rig.transport.togglePlayPause();
    }
    REQUIRE_FALSE(rig.transport.isPlaying()); // an even number of toggles
    REQUIRE(rig.transport.position() == 9'999);

    rig.blocks(1); // drains the queue; pump() sends the kept commands
    rig.blocks(1);
    REQUIRE_FALSE(rig.engine.status().playing);
    REQUIRE(rig.engine.status().position == 9'999);
    REQUIRE(rig.transport.position() == 9'999);
}
