#include "engine/GraphCompiler.h"

#include <algorithm>
#include <set>

namespace af::engine
{

namespace
{

dsp::FadeShape toShape(model::FadeCurve curve) noexcept
{
    switch (curve)
    {
    case model::FadeCurve::linear:
        return dsp::FadeShape::linear;
    case model::FadeCurve::equalPower:
        return dsp::FadeShape::equalPower;
    case model::FadeCurve::exponential:
        return dsp::FadeShape::exponential;
    case model::FadeCurve::sCurve:
        return dsp::FadeShape::sCurve;
    }
    return dsp::FadeShape::linear;
}

/// ceil(frames * to / from) without overflow for the supported ranges (24 h at 768 kHz).
core::FrameCount convertLengthCeil(core::FrameCount frames, core::SampleRate from,
                                   core::SampleRate to) noexcept
{
    return (frames * to + from - 1) / from;
}

} // namespace

GraphCompiler::GraphCompiler(assets::AudioBufferCache& cache)
    : cache_(cache)
{
}

std::shared_ptr<const dsp::SincKernel> GraphCompiler::kernelFor(core::SampleRate source,
                                                                core::SampleRate target)
{
    auto& kernel = kernels_[{source, target}];
    if (!kernel)
    {
        const double cutoff =
            std::min(1.0, static_cast<double>(target) / static_cast<double>(source));
        kernel = dsp::SincKernel::create(cutoff);
    }
    return kernel;
}

std::unique_ptr<RenderGraph> GraphCompiler::compile(const model::Project& project,
                                                    model::EventId eventId, const RenderSpec& spec,
                                                    CompileOptions options)
{
    RenderGraph::Contents contents;
    contents.needsCrossfade = options.needsCrossfade;
    std::set<assets::AssetKey> pending;

    const auto event = project.findEvent(eventId);
    if (event)
    {
        contents.masterGain = dsp::dbToGain(event->masterGainDb);
        const bool anySolo = std::any_of(event->layers.begin(), event->layers.end(),
                                         [](const model::LayerPtr& l) { return l && l->solo; });

        for (const auto& layer : event->layers)
        {
            if (!layer)
            {
                continue;
            }
            LayerNode node;
            node.audible = !layer->mute && (!anySolo || layer->solo);
            bool allMono = true;

            for (const auto& clip : layer->clips)
            {
                if (!clip)
                {
                    continue;
                }
                const auto asset = project.findAsset(clip->asset);
                if (!asset)
                {
                    continue;
                }
                if (asset->info.sampleRate > 0 && clip->sourceLengthFrames() > 0)
                {
                    const auto start = core::ticksToFramesNearest(clip->position, spec.sampleRate);
                    contents.endFrame = std::max(
                        contents.endFrame,
                        start + convertLengthCeil(clip->sourceLengthFrames(),
                                                  asset->info.sampleRate, spec.sampleRate));
                }
                auto audio = cache_.acquire(asset->id.uuid);
                if (!audio)
                {
                    pending.insert(asset->id.uuid);
                    continue;
                }
                const auto sourceLength = clip->sourceLengthFrames();
                if (sourceLength <= 0 || clip->sourceInFrames < 0 ||
                    clip->sourceOutFrames > audio->frames())
                {
                    continue; // inconsistent with the decoded file; validation reports it
                }

                ClipVoice voice;
                voice.sourceChannels = std::min(audio->channels(), 2);
                voice.sourceRate = audio->sampleRate();
                voice.renderRate = spec.sampleRate;
                voice.startFrame = core::ticksToFramesNearest(clip->position, spec.sampleRate);
                voice.lengthFrames =
                    convertLengthCeil(sourceLength, voice.sourceRate, voice.renderRate);
                voice.sourceIn = clip->sourceInFrames;
                voice.sourceLength = sourceLength;
                voice.reverse = clip->reverse;
                voice.gain = dsp::dbToGain(clip->gainDb);
                voice.fadeInFrames =
                    std::min(core::ticksToFramesNearest(clip->fadeIn.length, spec.sampleRate),
                             voice.lengthFrames);
                voice.fadeOutFrames =
                    std::min(core::ticksToFramesNearest(clip->fadeOut.length, spec.sampleRate),
                             voice.lengthFrames);
                voice.fadeInShape = toShape(clip->fadeIn.curve);
                voice.fadeOutShape = toShape(clip->fadeOut.curve);
                if (voice.sourceRate != voice.renderRate && spec.quality != RenderQuality::draft)
                {
                    voice.kernel = kernelFor(voice.sourceRate, voice.renderRate);
                }
                contents.endFrame = std::max(contents.endFrame, voice.endFrame());
                voice.audio = std::move(audio);
                allMono = allMono && voice.sourceChannels == 1;
                node.clips.push_back(std::move(voice));
            }

            node.monoBus = allMono && !node.clips.empty();
            const float fader = dsp::dbToGain(layer->gainDb);
            const auto pan =
                node.monoBus ? dsp::constantPowerPan(layer->pan) : dsp::balance(layer->pan);
            node.gains = {fader * pan.left, fader * pan.right};
            contents.layers.push_back(std::move(node));
        }
    }

    contents.pendingAssets.assign(pending.begin(), pending.end());
    return std::make_unique<RenderGraph>(spec, nextGeneration_++, std::move(contents));
}

std::vector<assets::AssetKey> GraphCompiler::referencedAssets(const model::Project& project,
                                                              model::EventId eventId)
{
    std::set<assets::AssetKey> keys;
    if (const auto event = project.findEvent(eventId))
    {
        for (const auto& layer : event->layers)
        {
            if (!layer)
            {
                continue;
            }
            for (const auto& clip : layer->clips)
            {
                if (clip)
                {
                    keys.insert(clip->asset.uuid);
                }
            }
        }
    }
    return {keys.begin(), keys.end()};
}

} // namespace af::engine
