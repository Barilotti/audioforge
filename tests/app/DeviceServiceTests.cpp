#include "AppTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace af;
using af::app::DeviceEvent;
using af::test::Harness;
using af::test::TempSettings;

TEST_CASE("The default device opens and plays the engine output", "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file()};

    const auto status = h.service->status();
    REQUIRE(status.open);
    REQUIRE(status.deviceName == "Fake A");
    REQUIRE(status.outputChannels == 2);
    REQUIRE(h.events.count(DeviceEvent::Kind::formatChanged) == 1);
    const auto spec = h.service->renderSpec();
    REQUIRE(spec.has_value());
    REQUIRE(spec->sampleRate == static_cast<core::SampleRate>(status.sampleRate));
    REQUIRE(spec->maxBlockFrames == status.bufferSize);

    REQUIRE(h.device()->runBlock() == 0.0f); // nothing published yet: silence
    h.playback->setTestTone(true);
    h.device()->runBlock();
    REQUIRE(h.device()->runBlock() > 0.05f);
    REQUIRE(h.service->engine().status().appliedGeneration > 0);
    REQUIRE(h.service->engine().status().playing);
    h.playback->setTestTone(false);
}

TEST_CASE("The user's device choice is saved and restored", "[app][devices]")
{
    const TempSettings settings;
    {
        Harness h{settings.file()};
        h.choose("Fake B", 96'000.0, 128);
        REQUIRE(settings.contents().find("Fake B") != std::string::npos);
    }
    Harness again{settings.file()};
    const auto status = again.service->status();
    REQUIRE(status.deviceName == "Fake B");
    REQUIRE(status.sampleRate == 96'000.0);
    REQUIRE(status.bufferSize == 128);
    REQUIRE_FALSE(status.usingFallback);
}

TEST_CASE("Unreadable settings fall back to the default device", "[app][devices]")
{
    const TempSettings settings;
    {
        std::ofstream out(settings.file());
        out << "<not valid xml";
    }
    Harness h{settings.file()};
    REQUIRE(h.service->status().deviceName == "Fake A");
    h.choose("Fake B", 48'000.0, 256);
    REQUIRE(settings.contents().find("<DEVICESETUP") != std::string::npos);
}

TEST_CASE("Changing rate or buffer while playing recompiles and keeps playing", "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.playback->setTestTone(true);
    REQUIRE(h.device()->runBlock() >= 0.0f);

    for (const auto& [rate, buffer] :
         std::vector<std::pair<double, int>>{{96'000.0, 64}, {44'100.0, 512}})
    {
        const auto before = h.events.count(DeviceEvent::Kind::formatChanged);
        h.choose("Fake A", rate, buffer);
        REQUIRE(h.events.count(DeviceEvent::Kind::formatChanged) == before + 1);
        REQUIRE(h.events.received.back().status.sampleRate == rate);
        REQUIRE(h.service->engine().status().rejectedGraphs == 0);
        h.device()->runBlock();
        REQUIRE(h.device()->runBlock() > 0.05f); // the tone was recompiled for the new format
        REQUIRE(h.playback->status().testTone);
        REQUIRE(h.service->transport().isPlaying());
    }
}

TEST_CASE("Unplugging the chosen device stops playback; plugging it back reopens it",
          "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.choose("Fake B", 48'000.0, 256);
    h.playback->setTestTone(true);
    h.device()->runBlock();
    REQUIRE(h.device()->runBlock() > 0.05f);

    h.unplug("Fake B");
    REQUIRE(h.events.count(DeviceEvent::Kind::deviceLost) == 1);
    REQUIRE_FALSE(h.service->transport().isPlaying());
    REQUIRE_FALSE(h.playback->status().testTone);
    auto status = h.service->status();
    REQUIRE(status.open);
    REQUIRE(status.deviceName == "Fake A"); // JUCE opened a fallback
    REQUIRE(status.usingFallback);
    REQUIRE(status.preferredName == "Fake B");
    REQUIRE(h.device()->runBlock() == 0.0f);
    REQUIRE_FALSE(h.service->engine().status().playing); // the pause reached the audio thread
    REQUIRE(settings.contents().find("Fake B") != std::string::npos); // the choice is kept

    h.plugIn("Fake B");
    REQUIRE(h.events.count(DeviceEvent::Kind::deviceRestored) == 1);
    status = h.service->status();
    REQUIRE(status.deviceName == "Fake B");
    REQUIRE_FALSE(status.usingFallback);
}

TEST_CASE("Losing every device leaves the application running silently", "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file(), {"Fake A"}};
    h.playback->setTestTone(true);

    h.unplug("Fake A");
    REQUIRE(h.events.count(DeviceEvent::Kind::noDevice) == 1);
    REQUIRE_FALSE(h.service->status().open);
    REQUIRE_FALSE(h.service->renderSpec().has_value());
    REQUIRE(h.service->engine().status().appliedGeneration == 0); // graphs were taken back
    REQUIRE_FALSE(h.service->transport().isPlaying());
    REQUIRE_FALSE(h.playback->status().testTone);

    h.plugIn("Fake A");
    REQUIRE(h.service->status().open);
    REQUIRE(h.service->status().deviceName == "Fake A");
}

TEST_CASE("Starting without any device works", "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file(), {}};
    REQUIRE_FALSE(h.service->status().open);
    REQUIRE(h.events.count(DeviceEvent::Kind::noDevice) == 1);
    h.playback->setTestTone(true); // nothing to play on, must not crash
    h.playback->refresh();
    h.playback->setTestTone(false);
}
