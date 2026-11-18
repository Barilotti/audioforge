#include "ui/timeline/TimelineModel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace af::ui
{

namespace
{

juce::String clipName(const model::AssetRef& asset)
{
    const std::filesystem::path path{
        std::u8string{asset.absolutePath.begin(), asset.absolutePath.end()}};
    const auto stem = path.stem().u8string();
    return juce::String::fromUTF8(reinterpret_cast<const char*>(stem.data()),
                                  static_cast<int>(stem.size()));
}

} // namespace

TimelineModel TimelineModel::fromProject(const model::Project& project, model::EventId eventId)
{
    TimelineModel result;
    const auto event = project.findEvent(eventId);
    if (!event)
    {
        return result;
    }
    for (const auto& layer : event->layers)
    {
        if (!layer)
        {
            continue;
        }
        TimelineLayer row;
        row.name = juce::String::fromUTF8(layer->name.c_str());
        row.mute = layer->mute;
        row.solo = layer->solo;
        for (const auto& clip : layer->clips)
        {
            if (!clip)
            {
                continue;
            }
            const auto asset = project.findAsset(clip->asset);
            if (!asset || asset->info.sampleRate <= 0)
            {
                continue;
            }
            TimelineClip item;
            item.start = clip->position.toSecondsApprox();
            item.length = static_cast<double>(clip->sourceLengthFrames()) /
                          static_cast<double>(asset->info.sampleRate);
            item.name = clipName(*asset);
            item.asset = asset->id.uuid;
            item.sourceIn = clip->sourceInFrames;
            item.sourceRate = asset->info.sampleRate;
            item.reverse = clip->reverse;
            item.sourceLength = clip->sourceLengthFrames();
            result.lengthSeconds = std::max(result.lengthSeconds, item.end());
            row.maxClipLength = std::max(row.maxClipLength, item.length);
            row.clips.push_back(std::move(item));
        }
        std::sort(row.clips.begin(), row.clips.end(),
                  [](const TimelineClip& a, const TimelineClip& b) { return a.start < b.start; });
        result.layers.push_back(std::move(row));
    }
    return result;
}

int TimelineClip::channels() const noexcept
{
    return peaks ? peaks->channels() : 0;
}

std::optional<assets::MinMax> TimelineClip::columnRange(int channel, double t0,
                                                        double t1) const noexcept
{
    if (!peaks || sourceRate <= 0 || channel < 0 || channel >= peaks->channels())
    {
        return std::nullopt;
    }
    const auto rate = static_cast<double>(sourceRate);
    // Offsets into the clip, in source frames, clamped to the clip.
    auto a = static_cast<core::FrameCount>(std::floor((t0 - start) * rate));
    auto b = static_cast<core::FrameCount>(std::ceil((t1 - start) * rate));
    a = std::clamp<core::FrameCount>(a, 0, sourceLength);
    b = std::clamp<core::FrameCount>(b, 0, sourceLength);
    if (b <= a)
    {
        if (a >= sourceLength || b <= 0)
        {
            return std::nullopt;
        }
        b = a + 1; // a pixel narrower than one frame shows the frame under it
    }
    // Reversed clips play the source backwards: offset o reads source frame in + len - 1 - o.
    const auto first = reverse ? sourceIn + sourceLength - b : sourceIn + a;
    const auto last = reverse ? sourceIn + sourceLength - a : sourceIn + b;

    if (last - first < assets::PeakData::blockFrames[0] && audio && channel < audio->channels())
    {
        const float* samples = audio->channel(channel);
        assets::MinMax result{samples[first], samples[first]};
        for (auto n = first + 1; n < last; ++n)
        {
            result.min = std::min(result.min, samples[n]);
            result.max = std::max(result.max, samples[n]);
        }
        return result;
    }
    return peaks->range(channel, first, last);
}

bool TimelineModel::updateAssets(const assets::AudioBufferCache& cache)
{
    bool changed = false;
    for (auto& layer : layers)
    {
        for (auto& clip : layer.clips)
        {
            if (clip.peaks && clip.audio)
            {
                continue;
            }
            const auto status = cache.status(clip.asset);
            const bool failed = status.state == assets::AssetState::failed;
            if (status.peaks != clip.peaks || status.audio != clip.audio || failed != clip.failed)
            {
                clip.peaks = status.peaks;
                clip.audio = status.audio;
                clip.failed = failed;
                changed = true;
            }
        }
    }
    return changed;
}

bool TimelineModel::waitingForAssets() const noexcept
{
    for (const auto& layer : layers)
    {
        for (const auto& clip : layer.clips)
        {
            if ((!clip.peaks || !clip.audio) && !clip.failed)
            {
                return true;
            }
        }
    }
    return false;
}

} // namespace af::ui
