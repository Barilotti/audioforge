#include "ui/timeline/TimelineModel.h"

#include <algorithm>
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

} // namespace af::ui
