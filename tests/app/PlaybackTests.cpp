#include "AppTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

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

std::vector<std::string> layerNames(const Playback& playback)
{
    std::vector<std::string> names;
    const auto& document = *playback.document();
    for (const auto& layer : document.project.findEvent(document.event)->layers)
    {
        names.push_back(layer->name);
    }
    return names;
}

} // namespace

TEST_CASE("An imported WAV file plays from the start and stops at its end", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    REQUIRE(h.playback->status().state == Playback::State::empty);

    h.playback->importFile(wavDir / "pcm_s24_stereo_96000.wav");
    REQUIRE(h.playback->status().state == Playback::State::loading);
    REQUIRE(h.playback->status().loadingCount == 1);
    waitForImport(h);
    const auto status = h.playback->status();
    REQUIRE(status.state == Playback::State::ready);
    REQUIRE(status.layerCount == 1);
    REQUIRE(status.lengthSeconds > 0.0);
    REQUIRE(layerNames(*h.playback) == std::vector<std::string>{"pcm_s24_stereo_96000"});

    auto& transport = h.playback->transport();
    transport.play();
    REQUIRE(h.device()->runBlock() > 0.01f); // 301 frames at 96 kHz: 151 frames at 48 kHz
    REQUIRE(h.service->engine().status().endFrame == 151);
    h.device()->runBlock();
    h.service->refresh(); // pumps the transport: the end was reached
    REQUIRE_FALSE(transport.isPlaying());
    REQUIRE(transport.position() == 0);
}

TEST_CASE("Ten files become ten layers in the order given", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    const std::vector<std::string> names{
        "pcm_s16_stereo_48000", "pcm_s24_stereo_96000",  "pcm_s32_mono_48000",
        "pcm_u8_mono_44100",    "float32_stereo_48000",  "float64_mono_48000",
        "bwf_s16_stereo_48000", "rf64_s24_stereo_48000", "extensible_s24_4ch_48000",
        "pcm_s16_stereo_48000"}; // the same file twice gives two layers
    std::vector<std::filesystem::path> files;
    files.reserve(names.size());
    for (const auto& name : names)
    {
        files.push_back(wavDir / (name + ".wav"));
    }
    h.playback->importFiles(files);
    waitForImport(h);

    REQUIRE(h.playback->status().layerCount == 10);
    REQUIRE(layerNames(*h.playback) == names);
    REQUIRE(h.playback->document()->project.assets.size() == 10);

    // All ten play together: ten layers of the fixture signal are louder than one.
    h.playback->transport().play();
    REQUIRE(h.device()->runBlock() > 0.5f);
}

TEST_CASE("Files that cannot be read add no layer and are reported once", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    const auto broken = settings.file().parent_path() / "broken.wav";
    {
        std::ofstream out(broken, std::ios::binary);
        out << "RIFF....WAVEjunk"; // a WAVE header without any chunk the decoder accepts
    }
    h.playback->importFiles(
        {wavDir / "pcm_s16_stereo_48000.wav", broken, wavDir / "float32_stereo_48000.wav"});
    waitForImport(h);

    REQUIRE(layerNames(*h.playback) ==
            std::vector<std::string>{"pcm_s16_stereo_48000", "float32_stereo_48000"});
    const auto problems = h.playback->takeProblems();
    REQUIRE(problems.size() == 1);
    REQUIRE(problems[0].fileName == "broken.wav");
    REQUIRE_FALSE(problems[0].imported);
    REQUIRE_FALSE(problems[0].message.empty());
    REQUIRE(h.playback->takeProblems().empty());

    h.playback->transport().play();
    REQUIRE(h.device()->runBlock() > 0.01f);
}

TEST_CASE("Layers added while playing keep the transport running", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.playback->importFile(wavDir / "float32_stereo_48000.wav");
    waitForImport(h);
    auto& transport = h.playback->transport();
    transport.play();
    h.device()->runBlock();
    const auto generation = h.service->engine().status().appliedGeneration;

    h.playback->importFile(wavDir / "pcm_s16_stereo_48000.wav");
    waitForImport(h);
    REQUIRE(transport.isPlaying());
    h.device()->runBlock();
    REQUIRE(h.service->engine().status().appliedGeneration > generation);
    REQUIRE(h.playback->status().layerCount == 2);
}

TEST_CASE("New removes every layer", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.playback->importFiles(
        {wavDir / "pcm_s16_stereo_48000.wav", wavDir / "float32_stereo_48000.wav"});
    waitForImport(h);
    const auto revision = h.playback->revision();
    const auto asset = h.playback->document()->project.assets.front()->id;

    h.playback->clear();
    REQUIRE(h.playback->revision() > revision);
    REQUIRE(h.playback->status().state == Playback::State::empty);
    REQUIRE(h.playback->document()->project.assets.empty());
    REQUIRE(h.cache.status(asset.uuid).state == assets::AssetState::unloaded); // forgotten
    h.device()->runBlock();
    REQUIRE(h.service->engine().status().endFrame == 0);
}

TEST_CASE("A device format change recompiles the layers", "[app][playback]")
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

TEST_CASE("The test tone takes the engine over and gives the layers back", "[app][playback]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.playback->importFile(wavDir / "pcm_s16_stereo_48000.wav");
    waitForImport(h);

    h.playback->setTestTone(true);
    REQUIRE(h.playback->testToneOn());
    h.device()->runBlock();
    REQUIRE(h.service->engine().status().endFrame == 4 * 48'000);
    REQUIRE(h.playback->transport().isPlaying());

    h.playback->setTestTone(false);
    REQUIRE_FALSE(h.playback->transport().isPlaying());
    h.device()->runBlock();
    REQUIRE(h.service->engine().status().endFrame == 301);
    REQUIRE(h.playback->transport().position() == 0);
}
