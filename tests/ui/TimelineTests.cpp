#include "model/Node.h"
#include "ui/timeline/TimelineGeometry.h"
#include "ui/timeline/TimelineModel.h"
#include "ui/timeline/TimelinePainter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using namespace af;
using af::ui::TimelineGeometry;
using af::ui::TimelineLayout;
using af::ui::TimelineModel;
using af::ui::TimelinePainter;
using Catch::Approx;

namespace
{

/// Median wall time of `frames` calls, in milliseconds. The median ignores frames where a
/// shared CI runner preempted the test, which made the mean fail at random.
template <typename Paint>
double medianFrameMs(int frames, Paint&& paint)
{
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(frames));
    for (int f = 0; f < frames; ++f)
    {
        const auto start = std::chrono::steady_clock::now();
        paint();
        times.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }
    std::nth_element(times.begin(), times.begin() + frames / 2, times.end());
    return times[static_cast<std::size_t>(frames / 2)];
}

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
    int i = 0;
    const auto perFrame =
        medianFrameMs(frames,
                      [&]
                      {
                          geometry.scrollByPixels(1.0);
                          TimelinePainter::paint(g, layout, geometry, model, 1.0 + 0.01 * i++);
                      });
    UNSCOPED_INFO("Timeline frame with 200 clips: " << perFrame << " ms");
#if defined(AF_SANITIZED_BUILD)
    // Sanitizers slow painting by an unpredictable factor: only check that it completes.
#elif defined(NDEBUG)
    REQUIRE(perFrame < 16.0);
#else
    REQUIRE(perFrame < 200.0);
#endif
}
