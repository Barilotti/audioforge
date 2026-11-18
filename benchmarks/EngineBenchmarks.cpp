// Engine benchmarks. Times are compared as ratios to BM_Calibration, a fixed arithmetic
// workload, so the regression check tolerates CI runners of different speed (ADR-0017).

#include "EngineTestSupport.h"
#include "assets/PeakData.h"

#include <algorithm>
#include <array>
#include <benchmark/benchmark.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

using namespace af;
using af::core::FrameCount;
using af::core::TimeTicks;

namespace
{

constexpr int layerCount = 64;
constexpr int assetCount = 8;     // layers share assets, as sound design projects do
constexpr int clipsPerLayer = 15; // 15 x 4 s: one minute of timeline
constexpr int clipSeconds = 4;

/// Deterministic noise in [-0.5, 0.5): no allocation, no library state.
float noise(std::uint32_t& state)
{
    state = state * 1'664'525u + 1'013'904'223u;
    return static_cast<float>(state >> 8) / static_cast<float>(1u << 24) - 0.5f;
}

/// 64 stereo layers, each a minute of back-to-back 4-second clips. `sourceRate` other than
/// 48 kHz makes every voice resample (the expensive path).
struct LayerProject
{
    test::EngineFixture fixture;

    explicit LayerProject(core::SampleRate sourceRate)
    {
        const auto sourceFrames = static_cast<FrameCount>(clipSeconds) * sourceRate;
        std::vector<model::AssetId> assets;
        for (int a = 0; a < assetCount; ++a)
        {
            std::uint32_t state = 12'345u + static_cast<std::uint32_t>(a);
            assets.push_back(fixture.addAsset(sourceRate, 2, sourceFrames,
                                              [&state](int, FrameCount) { return noise(state); }));
        }
        for (int l = 0; l < layerCount; ++l)
        {
            auto& layer = fixture.addLayer();
            for (int c = 0; c < clipsPerLayer; ++c)
            {
                const auto position = TimeTicks::fromSeconds(std::int64_t{clipSeconds} * c);
                auto& clip =
                    fixture.addClip(layer, assets[static_cast<std::size_t>((l + c) % assetCount)],
                                    position, 0, sourceFrames);
                clip.fadeIn.length = TimeTicks{TimeTicks::perSecond / 100}; // 10 ms
                clip.fadeOut.length = TimeTicks{TimeTicks::perSecond / 100};
            }
        }
    }
};

/// Fixed scalar work (a one-pole filter over 64k samples): the yardstick for other times.
void BM_Calibration(benchmark::State& state)
{
    std::vector<float> input(65'536);
    std::uint32_t seed = 1;
    for (auto& x : input)
    {
        x = noise(seed);
    }
    for (auto _ : state)
    {
        float y = 0.0f;
        for (const float x : input)
        {
            y = 0.999f * y + 0.001f * std::tanh(x * 4.0f);
        }
        benchmark::DoNotOptimize(y);
    }
}
BENCHMARK(BM_Calibration);

/// One audio block of 64 playing layers. check_regression.py reports the load: time per block
/// divided by the block's real-time duration (1.0 = one core fully busy).
void renderLayers(benchmark::State& state, core::SampleRate sourceRate)
{
    const int block = static_cast<int>(state.range(0));
    const engine::RenderSpec spec{48'000, block};
    LayerProject project{sourceRate};
    engine::AudioEngine engine;
    engine.prepare(spec.sampleRate, block);
    engine.publish(project.fixture.compile(spec));
    (void)engine.transport(engine::TransportCommand::play());

    std::vector<float> left(static_cast<std::size_t>(block));
    std::vector<float> right(static_cast<std::size_t>(block));
    std::array<float*, 2> outputs{left.data(), right.data()};
    const auto end = static_cast<FrameCount>(clipSeconds) * clipsPerLayer * spec.sampleRate;
    FrameCount position = 0;
    for (auto _ : state)
    {
        engine.process(outputs.data(), 2, block);
        benchmark::DoNotOptimize(left.data());
        position += block;
        if (position + 2 * block >= end)
        {
            state.PauseTiming();
            (void)engine.transport(engine::TransportCommand::seek(0));
            position = 0;
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations() * block);
}

void BM_Engine64Layers(benchmark::State& state)
{
    renderLayers(state, 48'000);
}
BENCHMARK(BM_Engine64Layers)->Arg(128)->Arg(256)->Arg(512)->Unit(benchmark::kMicrosecond);

void BM_Engine64LayersResampled(benchmark::State& state)
{
    renderLayers(state, 44'100);
}
BENCHMARK(BM_Engine64LayersResampled)->Arg(128)->Arg(512)->Unit(benchmark::kMicrosecond);

/// Compiling the 64-layer document (every edit pays this on the message thread).
void BM_Compile64Layers(benchmark::State& state)
{
    LayerProject project{44'100};
    const engine::RenderSpec spec{48'000, 512};
    const auto document = project.fixture.project();
    for (auto _ : state)
    {
        auto graph = project.fixture.compiler().compile(document, project.fixture.event().id, spec);
        benchmark::DoNotOptimize(graph.get());
    }
}
BENCHMARK(BM_Compile64Layers)->Unit(benchmark::kMicrosecond);

/// Building the peak mipmap for one minute of stereo audio (part of every import).
void BM_PeakBuildOneMinute(benchmark::State& state)
{
    constexpr FrameCount frames = 60 * 48'000;
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    std::uint32_t seed = 7;
    for (FrameCount i = 0; i < frames; ++i)
    {
        left[static_cast<std::size_t>(i)] = noise(seed);
        right[static_cast<std::size_t>(i)] = noise(seed);
    }
    for (auto _ : state)
    {
        assets::PeakBuilder builder{48'000, 2, frames};
        constexpr FrameCount chunk = 65'536;
        for (FrameCount done = 0; done < frames; done += chunk)
        {
            const auto n = std::min(chunk, frames - done);
            const std::array<const float*, 2> channels{left.data() + done, right.data() + done};
            builder.add(std::span<const float* const>{channels}, n);
        }
        benchmark::DoNotOptimize(builder.finish());
    }
}
BENCHMARK(BM_PeakBuildOneMinute)->Unit(benchmark::kMillisecond);

} // namespace
