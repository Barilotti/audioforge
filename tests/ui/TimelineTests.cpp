#include "assets/AudioBufferCache.h"
#include "assets/PeakData.h"
#include "model/Node.h"
#include "ui/timeline/TimelineGeometry.h"
#include "ui/timeline/TimelineModel.h"
#include "ui/timeline/TimelinePainter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <random>

using namespace af;
using af::ui::TimelineGeometry;
using af::ui::TimelineLayout;
using af::ui::TimelineModel;
using af::ui::TimelinePainter;
using Catch::Approx;

namespace
{

/// `layers` layers of `clipsPerLayer` one-second clips with one-second gaps.
model::Project makeProject(int layers, int clipsPerLayer, model::EventId& eventId)
{
    model::Project project;
    project.id = model::ProjectId::generate();
    model::AssetRef asset{
        model::AssetId::generate(), "/sounds/door slam.wav", {}, {48'000, 2, 48'000}};
    project.assets.push_back(model::makeNode(asset));
    model::Event event;
    event.id = model::EventId::generate();
    for (int l = 0; l < layers; ++l)
    {
        model::Layer layer;
        layer.id = model::LayerId::generate();
        layer.name = "Layer " + std::to_string(l + 1);
        for (int c = clipsPerLayer - 1; c >= 0; --c) // reversed: the model must sort them
        {
            model::Clip clip;
            clip.id = model::ClipId::generate();
            clip.asset = asset.id;
            clip.position = core::TimeTicks::fromSeconds(std::int64_t{2} * c);
            clip.sourceOutFrames = 48'000;
            layer.clips.push_back(model::makeNode(clip));
        }
        event.layers.push_back(model::makeNode(layer));
    }
    eventId = event.id;
    project.events.push_back(model::makeNode(event));
    return project;
}

} // namespace

TEST_CASE("Timeline geometry maps time and pixels and zooms around the mouse", "[ui][timeline]")
{
    TimelineGeometry g;
    g.setWidth(1000.0);
    g.setContentSeconds(100.0);
    g.zoomToFit();
    REQUIRE(g.viewStart() == 0.0);
    REQUIRE(g.visibleSeconds() == Approx(102.0));
    REQUIRE(g.timeForX(g.xForTime(37.5)) == Approx(37.5));

    const double anchorTime = g.timeForX(250.0);
    g.zoomAround(0.1, 250.0);
    REQUIRE(g.timeForX(250.0) == Approx(anchorTime));
    REQUIRE(g.visibleSeconds() == Approx(10.2));

    g.setViewStart(-50.0);
    REQUIRE(g.viewStart() == 0.0); // clamped at the start
    g.setViewStart(1e9);
    REQUIRE(g.viewEnd() == Approx(g.scrollableSeconds())); // and at the end

    for (int i = 0; i < 100; ++i)
    {
        g.zoomAround(0.5, 0.0);
    }
    REQUIRE(g.secondsPerPixel() == TimelineGeometry::minSecondsPerPixel);

    g.zoomToFit();
    g.zoomAround(0.01, 0.0);
    g.reveal(50.0);
    REQUIRE(g.viewStart() <= 50.0);
    REQUIRE(g.viewEnd() >= 50.0);
}

TEST_CASE("Ruler steps are round numbers at least the minimum width apart", "[ui][timeline]")
{
    TimelineGeometry g;
    g.setWidth(1000.0);
    for (double spp = TimelineGeometry::minSecondsPerPixel; spp < 30.0; spp *= 1.7)
    {
        g.setContentSeconds(1e6);
        g.zoomAround(spp / g.secondsPerPixel(), 0.0);
        const double step = g.rulerStep(80.0);
        REQUIRE(step / g.secondsPerPixel() >= 80.0 - 1e-6);
        REQUIRE(step / g.secondsPerPixel() < 80.0 * 5.0); // not needlessly coarse
    }
    REQUIRE(TimelinePainter::formatRulerTime(75.0, 5.0) == "1:15");
    REQUIRE(TimelinePainter::formatRulerTime(1.25, 0.05) == "0:01.25");
}

TEST_CASE("The timeline model lists layers in order and clips sorted by time", "[ui][timeline]")
{
    model::EventId eventId;
    const auto project = makeProject(3, 4, eventId);
    const auto model = TimelineModel::fromProject(project, eventId);
    REQUIRE(model.layers.size() == 3);
    REQUIRE(model.layers[0].name == "Layer 1");
    REQUIRE(model.layers[2].clips.size() == 4);
    REQUIRE(model.layers[2].clips[0].start == 0.0);
    REQUIRE(model.layers[2].clips[3].start == 6.0);
    REQUIRE(model.layers[2].clips[3].length == 1.0);
    REQUIRE(model.layers[2].clips[3].name == "door slam");
    REQUIRE(model.lengthSeconds == 7.0);
    REQUIRE(TimelineModel::fromProject(project, model::EventId::generate()).layers.empty());
}

TEST_CASE("Only visible layers and clips are drawn", "[ui][timeline]")
{
    model::EventId eventId;
    const auto project = makeProject(200, 50, eventId); // 10 000 clips over 100 s
    const auto model = TimelineModel::fromProject(project, eventId);

    juce::Image image{juce::Image::RGB, 1400, 800, true};
    juce::Graphics g{image};
    auto layout = TimelineLayout::forArea(image.getBounds());
    TimelineGeometry geometry;
    geometry.setWidth(layout.lanes.getWidth());
    geometry.setContentSeconds(model.lengthSeconds);
    geometry.zoomAround(10.0 / geometry.visibleSeconds(), 0.0); // 10 s visible
    geometry.setViewStart(21.0);
    layout.verticalScroll = 50 * TimelineLayout::layerHeight + 10;

    const auto [first, last] = layout.visibleLayers(model.layers.size());
    REQUIRE(first == 50);
    REQUIRE(last - first == 11); // 772 px of lanes / 72 px rows, one cut at each edge

    const int drawn = TimelinePainter::paint(g, layout, geometry, model, 25.0);
    // Clips at 20, 22, ... 30 s touch [21, 31): 6 per visible layer.
    REQUIRE(drawn == 6 * static_cast<int>(last - first));
}

TEST_CASE("Two hundred visible clips paint within a 60 FPS frame", "[ui][timeline][performance]")
{
    model::EventId eventId;
    const auto project = makeProject(10, 20, eventId);
    const auto model = TimelineModel::fromProject(project, eventId);

    juce::Image image{juce::Image::RGB, 1600, 900, true};
    juce::Graphics g{image};
    auto layout = TimelineLayout::forArea(image.getBounds());
    TimelineGeometry geometry;
    geometry.setWidth(layout.lanes.getWidth());
    geometry.setContentSeconds(model.lengthSeconds);
    geometry.zoomToFit();

    REQUIRE(TimelinePainter::paint(g, layout, geometry, model, 1.0) == 200);
#if defined(AF_SANITIZED_BUILD)
    constexpr int frames = 5;
#else
    constexpr int frames = 60;
#endif
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i)
    {
        geometry.scrollByPixels(1.0);
        TimelinePainter::paint(g, layout, geometry, model, 1.0 + 0.01 * i);
    }
    const auto perFrame =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count() /
        frames;
    UNSCOPED_INFO("Timeline frame with 200 clips: " << perFrame << " ms");
#if defined(AF_SANITIZED_BUILD)
    // Sanitizers slow painting by an unpredictable factor: only check that it completes.
#elif defined(NDEBUG)
    REQUIRE(perFrame < 16.0);
#else
    REQUIRE(perFrame < 200.0);
#endif
}

namespace
{

/// A clip over frames [in, in + length) of a random stereo asset, with peaks and audio.
struct WaveClip
{
    std::shared_ptr<assets::DecodedAudio> audio;
    ui::TimelineClip clip;

    WaveClip(core::FrameCount in, core::FrameCount length, bool reverse)
    {
        constexpr core::FrameCount frames = 500'000;
        audio = std::make_shared<assets::DecodedAudio>(48'000, 2, frames);
        std::mt19937 rng{17};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
        for (int ch = 0; ch < 2; ++ch)
        {
            std::generate_n(audio->writableChannel(ch), frames, [&] { return dist(rng); });
        }
        assets::PeakBuilder builder{48'000, 2, frames};
        std::array<const float*, 2> channels{audio->channel(0), audio->channel(1)};
        builder.add(channels, frames);
        clip.peaks = builder.finish();
        clip.audio = audio;
        clip.start = 3.0;
        clip.sourceIn = in;
        clip.sourceLength = length;
        clip.sourceRate = 48'000;
        clip.length = static_cast<double>(length) / 48'000.0;
        clip.reverse = reverse;
    }

    /// Exact min/max of the source frames that timeline [t0, t1) plays.
    [[nodiscard]] assets::MinMax exact(int channel, double t0, double t1) const
    {
        auto a = static_cast<core::FrameCount>(std::floor((t0 - clip.start) * 48'000.0));
        auto b = static_cast<core::FrameCount>(std::ceil((t1 - clip.start) * 48'000.0));
        a = std::clamp<core::FrameCount>(a, 0, clip.sourceLength);
        b = std::clamp<core::FrameCount>(b, a + 1, clip.sourceLength);
        const auto first = clip.reverse ? clip.sourceIn + clip.sourceLength - b : clip.sourceIn + a;
        const auto last = clip.reverse ? clip.sourceIn + clip.sourceLength - a : clip.sourceIn + b;
        const float* s = audio->channel(channel);
        const auto [lo, hi] = std::minmax_element(s + first, s + last);
        return {*lo, *hi};
    }
};

} // namespace

TEST_CASE("Waveform columns are correct at every zoom level", "[ui][timeline][waveform]")
{
    const bool reverse = GENERATE(false, true);
    const WaveClip w{12'345, 400'000, reverse};

    // From several samples per pixel up to far more than the coarsest peak block.
    for (const double framesPerPixel :
         {0.25, 1.0, 7.0, 100.0, 255.0, 256.0, 1000.0, 5000.0, 70'000.0, 200'000.0})
    {
        const double secondsPerPixel = framesPerPixel / 48'000.0;
        for (int column = 0; column < 40; ++column)
        {
            const double t0 = w.clip.start + 0.1 + column * secondsPerPixel * 3.7;
            const double t1 = t0 + secondsPerPixel;
            if (t1 > w.clip.end())
            {
                break;
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto shown = w.clip.columnRange(ch, t0, t1);
                REQUIRE(shown.has_value());
                const auto exact = w.exact(ch, t0, t1);
                // Never hides a peak; exact where samples are used (finer than 256 frames).
                REQUIRE(shown->min <= exact.min);
                REQUIRE(shown->max >= exact.max);
                if (framesPerPixel < 255.0)
                {
                    REQUIRE(*shown == exact);
                }
            }
        }
    }
    REQUIRE_FALSE(w.clip.columnRange(0, 0.0, 1.0).has_value());     // before the clip
    REQUIRE_FALSE(w.clip.columnRange(0, 100.0, 101.0).has_value()); // after it
}

TEST_CASE("Without audio in memory the waveform falls back to the peaks",
          "[ui][timeline][waveform]")
{
    WaveClip w{0, 100'000, false};
    w.clip.audio.reset(); // evicted: only peaks remain
    const double t0 = w.clip.start + 0.5;
    const auto shown = w.clip.columnRange(0, t0, t0 + 10.0 / 48'000.0);
    REQUIRE(shown.has_value());
    const auto exact = w.exact(0, t0, t0 + 10.0 / 48'000.0);
    REQUIRE(shown->min <= exact.min);
    REQUIRE(shown->max >= exact.max);

    w.clip.peaks.reset(); // still loading: nothing to draw
    REQUIRE_FALSE(w.clip.columnRange(0, t0, t0 + 0.01).has_value());
}

TEST_CASE("Clips with waveforms still paint within a 60 FPS frame", "[ui][timeline][performance]")
{
    model::EventId eventId;
    const auto project = makeProject(10, 20, eventId);
    auto model = TimelineModel::fromProject(project, eventId);
    const WaveClip w{0, 48'000, false};
    for (auto& layer : model.layers)
    {
        for (auto& clip : layer.clips)
        {
            clip.peaks = w.clip.peaks;
            clip.audio = w.clip.audio;
        }
    }

    juce::Image image{juce::Image::RGB, 1600, 900, true};
    juce::Graphics g{image};
    auto layout = TimelineLayout::forArea(image.getBounds());
    TimelineGeometry geometry;
    geometry.setWidth(layout.lanes.getWidth());
    geometry.setContentSeconds(model.lengthSeconds);
    geometry.zoomToFit();

    constexpr int frames = 30;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i)
    {
        REQUIRE(TimelinePainter::paint(g, layout, geometry, model, 1.0) == 200);
    }
    const auto perFrame =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count() /
        frames;
    UNSCOPED_INFO("Timeline frame with 200 waveforms: " << perFrame << " ms");
#if defined(NDEBUG) && !defined(AF_SANITIZED_BUILD)
    REQUIRE(perFrame < 16.0);
#else
    REQUIRE(perFrame < 500.0);
#endif
}
