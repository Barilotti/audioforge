#pragma once

#include "assets/AudioBufferCache.h"
#include "engine/AudioEngine.h"
#include "engine/GraphCompiler.h"
#include "model/Node.h"
#include "model/Project.h"

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace af::test
{

/// Builds a one-event project around audio generated in memory and inserted into a cache.
class EngineFixture
{
public:
    EngineFixture()
        : cache_(makeOptions())
    {
        event_.id = model::EventId::generate();
        project_.id = model::ProjectId::generate();
    }

    /// Adds an asset whose samples come from `signal(channel, frame)`.
    model::AssetId addAsset(core::SampleRate rate, int channels, core::FrameCount frames,
                            const std::function<float(int, core::FrameCount)>& signal)
    {
        auto audio = std::make_shared<assets::DecodedAudio>(rate, channels, frames);
        for (int c = 0; c < channels; ++c)
        {
            for (core::FrameCount i = 0; i < frames; ++i)
            {
                audio->writableChannel(c)[i] = signal(c, i);
            }
        }
        model::AssetRef ref{
            model::AssetId::generate(), "/generated.wav", {}, {rate, channels, frames}};
        (void)cache_.insertDecoded(ref.id.uuid, audio);
        project_.assets.push_back(model::makeNode(ref));
        return ref.id;
    }

    /// Adds a layer and returns it for editing; clips are added with addClip().
    model::Layer& addLayer()
    {
        model::Layer layer;
        layer.id = model::LayerId::generate();
        layers_.push_back(layer);
        return layers_.back();
    }

    model::Clip& addClip(model::Layer& layer, model::AssetId asset, core::TimeTicks position,
                         core::FrameCount sourceIn, core::FrameCount sourceOut)
    {
        model::Clip clip;
        clip.id = model::ClipId::generate();
        clip.asset = asset;
        clip.position = position;
        clip.sourceInFrames = sourceIn;
        clip.sourceOutFrames = sourceOut;
        clips_.emplace_back(&layer, clip);
        return clips_.back().second;
    }

    [[nodiscard]] model::Project project()
    {
        auto event = event_;
        for (auto& layer : layers_)
        {
            auto copy = layer;
            for (auto& [owner, clip] : clips_)
            {
                if (owner == &layer)
                {
                    copy.clips.push_back(model::makeNode(clip));
                }
            }
            event.layers.push_back(model::makeNode(copy));
        }
        auto project = project_;
        project.events = {model::makeNode(event)};
        return project;
    }

    [[nodiscard]] std::unique_ptr<engine::RenderGraph> compile(const engine::RenderSpec& spec,
                                                               engine::CompileOptions options = {})
    {
        const auto p = project();
        return compiler_.compile(p, event_.id, spec, options);
    }

    model::Event& event() { return event_; }
    assets::AudioBufferCache& cache() { return cache_; }
    engine::GraphCompiler& compiler() { return compiler_; }

private:
    static assets::AudioBufferCache::Options makeOptions()
    {
        assets::AudioBufferCache::Options o;
        o.budget = assets::MemoryBudget::defaultsFor(16 * assets::gibibyte);
        o.workerThreads = 1;
        return o;
    }

    assets::AudioBufferCache cache_;
    engine::GraphCompiler compiler_{cache_};
    model::Project project_;
    model::Event event_;
    std::deque<model::Layer> layers_; // deque: references stay valid while tests add more
    std::deque<std::pair<model::Layer*, model::Clip>> clips_;
};

struct StereoBuffer
{
    std::vector<float> left;
    std::vector<float> right;
};

/// Plays `frames` timeline frames from frame 0 through an engine, in blocks whose sizes come
/// from `nextBlock` (each at most maxBlock).
inline StereoBuffer playThroughEngine(std::unique_ptr<engine::RenderGraph> graph,
                                      core::FrameCount frames,
                                      const std::function<int()>& nextBlock)
{
    engine::AudioEngine engine;
    engine.prepare(graph->spec().sampleRate, graph->spec().maxBlockFrames);
    engine.publish(std::move(graph));
    (void)engine.transport(engine::TransportCommand::play()); // an empty queue has room

    StereoBuffer out{std::vector<float>(static_cast<std::size_t>(frames)),
                     std::vector<float>(static_cast<std::size_t>(frames))};
    core::FrameCount done = 0;
    while (done < frames)
    {
        const auto n = static_cast<int>(std::min<core::FrameCount>(nextBlock(), frames - done));
        std::array<float*, 2> channels{out.left.data() + done, out.right.data() + done};
        engine.process(channels.data(), 2, n);
        done += n;
    }
    return out;
}

} // namespace af::test
