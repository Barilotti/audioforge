#include "model/Project.h"

#include <algorithm>

namespace af::model
{

AssetPtr Project::findAsset(AssetId asset) const noexcept
{
    const auto it = std::find_if(assets.begin(), assets.end(),
                                 [asset](const AssetPtr& a) { return a && a->id == asset; });
    return it != assets.end() ? *it : nullptr;
}

EventPtr Project::findEvent(EventId event) const noexcept
{
    const auto it = std::find_if(events.begin(), events.end(),
                                 [event](const EventPtr& e) { return e && e->id == event; });
    return it != events.end() ? *it : nullptr;
}

ProjectPtr makeEmptyProject()
{
    Event event;
    event.id = EventId::generate();
    event.name = "Untitled Event";

    Project project;
    project.id = ProjectId::generate();
    project.events.push_back(makeNode(std::move(event)));
    return makeNode(std::move(project));
}

core::TimeTicks clipTimelineLength(const Clip& clip, const AssetInfo& asset) noexcept
{
    return core::framesToTicks(clip.sourceLengthFrames(), asset.sampleRate);
}

core::TimeTicks eventEnd(const Event& event, const Project& project) noexcept
{
    core::TimeTicks end{};
    for (const auto& layer : event.layers)
    {
        if (!layer)
        {
            continue;
        }
        for (const auto& clip : layer->clips)
        {
            if (!clip)
            {
                continue;
            }
            const auto asset = project.findAsset(clip->asset);
            if (!asset || !core::isSupportedSampleRate(asset->info.sampleRate))
            {
                continue;
            }
            end = std::max(end, clip->position + clipTimelineLength(*clip, asset->info));
        }
    }
    return end;
}

} // namespace af::model
