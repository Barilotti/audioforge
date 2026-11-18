#include "app/TestTone.h"

#include "model/Node.h"

#include <cmath>
#include <numbers>

namespace af::app
{

namespace
{

constexpr core::SampleRate toneRate = 48'000;
constexpr core::FrameCount toneFrames = toneRate * 4;
constexpr double toneHz = 440.0;
constexpr double toneAmplitude = 0.25; // -12 dBFS, then -3 dB from the centre pan

} // namespace

TestToneProject makeTestTone(assets::AudioBufferCache& cache)
{
    auto audio = std::make_shared<assets::DecodedAudio>(toneRate, 1, toneFrames);
    float* samples = audio->writableChannel(0);
    for (core::FrameCount i = 0; i < toneFrames; ++i)
    {
        samples[i] =
            static_cast<float>(toneAmplitude * std::sin(2.0 * std::numbers::pi * toneHz *
                                                        static_cast<double>(i) / toneRate));
    }

    model::AssetRef asset{model::AssetId::generate(), "test-tone", {}, {toneRate, 1, toneFrames}};
    (void)cache.insertDecoded(asset.id.uuid, std::move(audio));

    model::Clip clip;
    clip.id = model::ClipId::generate();
    clip.asset = asset.id;
    clip.sourceOutFrames = toneFrames;
    clip.fadeIn = {core::framesToTicks(480, toneRate), model::FadeCurve::sCurve};
    clip.fadeOut = {core::framesToTicks(480, toneRate), model::FadeCurve::sCurve};

    model::Layer layer;
    layer.id = model::LayerId::generate();
    layer.name = "Test tone";
    layer.clips.push_back(model::makeNode(clip));

    model::Event event;
    event.id = model::EventId::generate();
    event.name = "Test tone";
    event.layers.push_back(model::makeNode(layer));

    TestToneProject result;
    result.event = event.id;
    result.project.id = model::ProjectId::generate();
    result.project.assets.push_back(model::makeNode(asset));
    result.project.events.push_back(model::makeNode(event));
    return result;
}

} // namespace af::app
