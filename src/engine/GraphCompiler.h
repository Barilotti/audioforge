#pragma once

#include "assets/AudioBufferCache.h"
#include "dsp/SincKernel.h"
#include "engine/RenderGraph.h"
#include "engine/RenderSpec.h"
#include "model/Ids.h"
#include "model/Project.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace af::engine
{

struct CompileOptions
{
    /// The new graph replaces the playing one with a 256-sample crossfade (structural edits
    /// near the playhead, architecture v0.2, section 4.2).
    bool needsCrossfade = false;
};

/// Turns a document snapshot into a RenderGraph (architecture v0.2, section 4.8). Runs off the
/// audio thread; allocation is allowed. One compiler is used by one thread at a time.
class GraphCompiler
{
public:
    explicit GraphCompiler(assets::AudioBufferCache& cache);

    /// Compiles `event` of `project` for `spec`. Clips whose asset is not decoded yet are
    /// skipped (they play silence) and listed in RenderGraph::pendingAssets(); recompile when
    /// the cache reports them ready. A missing event compiles to a silent graph.
    [[nodiscard]] std::unique_ptr<RenderGraph> compile(const model::Project& project,
                                                       model::EventId event, const RenderSpec& spec,
                                                       CompileOptions options = {});

    /// Every asset the event refers to: the set to pin in the AudioBufferCache.
    [[nodiscard]] static std::vector<assets::AssetKey>
    referencedAssets(const model::Project& project, model::EventId event);

private:
    std::shared_ptr<const dsp::SincKernel> kernelFor(core::SampleRate source,
                                                     core::SampleRate target);

    assets::AudioBufferCache& cache_;
    std::uint64_t nextGeneration_ = 1;
    std::map<std::pair<core::SampleRate, core::SampleRate>, std::shared_ptr<const dsp::SincKernel>>
        kernels_;
};

} // namespace af::engine
