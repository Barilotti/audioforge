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

assets::AudioBufferCache::Options cacheOptions()
{
    assets::AudioBufferCache::Options o;
    o.budget.softBytes = 64 * assets::mebibyte;
    o.budget.hardBytes = 64 * assets::mebibyte;
    o.workerThreads = 1;
    return o;
}

} // namespace

TestTone::TestTone(DeviceService& devices)
    : devices_(devices)
    , cache_(cacheOptions())
    , compiler_(cache_)
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
    (void)cache_.insertDecoded(asset.id.uuid, std::move(audio));

    model::Clip clip;
    clip.id = model::ClipId::generate();
    clip.asset = asset.id;
    clip.sourceOutFrames = toneFrames;
    clip.fadeIn = {core::framesToTicks(480, toneRate), model::FadeCurve::sCurve};
    clip.fadeOut = {core::framesToTicks(480, toneRate), model::FadeCurve::sCurve};
    length_ = core::framesToTicks(toneFrames, toneRate);

    model::Layer layer;
    layer.id = model::LayerId::generate();
    layer.name = "Test tone";
    layer.clips.push_back(model::makeNode(clip));

    model::Event event;
    event.id = model::EventId::generate();
    event.layers.push_back(model::makeNode(layer));
    event_ = event.id;

    project_.id = model::ProjectId::generate();
    project_.assets.push_back(model::makeNode(asset));
    project_.events.push_back(model::makeNode(event));

    devices_.addListener(this);
}

TestTone::~TestTone()
{
    stop();
    devices_.removeListener(this);
}

void TestTone::start()
{
    playing_ = true;
    publishGraph();
    devices_.engine().seek(0);
    devices_.engine().setPlaying(true);
    startTimerHz(30);
}

void TestTone::stop()
{
    if (!playing_)
    {
        return;
    }
    playing_ = false;
    stopTimer();
    devices_.engine().setPlaying(false);
}

void TestTone::publishGraph()
{
    if (const auto spec = devices_.renderSpec())
    {
        devices_.engine().publish(compiler_.compile(project_, event_, *spec));
        devices_.engine().collectRetired();
    }
}

void TestTone::deviceEvent(const DeviceEvent& event)
{
    if (!playing_)
    {
        return;
    }
    switch (event.kind)
    {
    case DeviceEvent::Kind::formatChanged:
        publishGraph(); // the device restarted: the engine dropped its graphs
        devices_.engine().setPlaying(true);
        break;
    case DeviceEvent::Kind::deviceLost:
    case DeviceEvent::Kind::noDevice:
        stop();
        break;
    case DeviceEvent::Kind::deviceRestored:
        break;
    }
}

void TestTone::timerCallback()
{
    const auto status = devices_.engine().status();
    if (const auto spec = devices_.renderSpec())
    {
        if (status.position >= core::ticksToFramesNearest(length_, spec->sampleRate))
        {
            devices_.engine().seek(0); // loop
        }
    }
}

} // namespace af::app
