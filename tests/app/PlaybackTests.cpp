#include "AppTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace af;
using af::app::Playback;
using af::test::Harness;
using af::test::TempSettings;

namespace
{

const std::filesystem::path wavDir = std::filesystem::path{AF_TEST_DATA_DIR} / "wav";

void waitForImport(Harness& h)
{
    REQUIRE(h.cache.waitUntilIdle(std::chrono::seconds(10)));
    h.playback->refresh();
}

} // namespace

TEST_CASE("An imported WAV file plays from the start and stops at its end", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    REQUIRE(h.playback->status().state == Playback::State::empty);

    h.playback->importFile(wavDir / "pcm_s24_stereo_96000.wav");
    REQUIRE(h.playback->status().state == Playback::State::loading);
    REQUIRE(h.playback->status().loadingName == "pcm_s24_stereo_96000.wav");
    waitForImport(h);
    const auto status = h.playback->status();
    REQUIRE(status.state == Playback::State::ready);
    REQUIRE(status.fileName == "pcm_s24_stereo_96000.wav");
    REQUIRE(status.lengthSeconds > 0.0);

    auto& transport = h.playback->transport();
    transport.play();
    REQUIRE(h.device()->runBlock() > 0.01f); // 301 frames at 96 kHz: 151 frames at 48 kHz
    REQUIRE(h.service->engine().status().endFrame == 151);
    h.device()->runBlock();
    h.service->refresh(); // pumps the transport: the end was reached
    REQUIRE_FALSE(transport.isPlaying());
    REQUIRE(transport.position() == 0);
}

TEST_CASE("A failed import keeps the previous file playable", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.playback->importFile(wavDir / "pcm_s16_stereo_48000.wav");
    waitForImport(h);
    REQUIRE(h.playback->status().state == Playback::State::ready);

    const auto broken = settings.file().parent_path() / "broken.wav";
    {
        std::ofstream out(broken, std::ios::binary);
        out << "RIFF....WAVEjunk"; // a WAVE header without any chunk the decoder accepts
    }
    h.playback->importFile(broken);
    waitForImport(h);
    const auto status = h.playback->status();
    REQUIRE(status.state == Playback::State::failed);
    REQUIRE(status.loadingName == "broken.wav");
    REQUIRE_FALSE(status.error.empty());
    REQUIRE(status.fileName == "pcm_s16_stereo_48000.wav");

    h.playback->transport().play();
    REQUIRE(h.device()->runBlock() > 0.01f);
}

TEST_CASE("A device format change recompiles the imported file", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.playback->importFile(wavDir / "float32_stereo_48000.wav");
    waitForImport(h);
    h.choose("Fake A", 96'000.0, 128);
    REQUIRE(h.service->engine().status().rejectedGraphs == 0);
    h.playback->transport().play();
    REQUIRE(h.device()->runBlock() > 0.01f);
    REQUIRE(h.service->engine().status().endFrame == 602); // 301 frames, now at 96 kHz
}

TEST_CASE("The test tone takes the engine over and gives the file back", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.playback->importFile(wavDir / "pcm_s16_stereo_48000.wav");
    waitForImport(h);

    h.playback->setTestTone(true);
    h.device()->runBlock();
    REQUIRE(h.service->engine().status().endFrame == 4 * 48'000);
    REQUIRE(h.playback->transport().isPlaying());

    h.playback->setTestTone(false);
    REQUIRE_FALSE(h.playback->transport().isPlaying());
    h.device()->runBlock();
    REQUIRE(h.service->engine().status().endFrame == 301);
    REQUIRE(h.playback->transport().position() == 0);
}
