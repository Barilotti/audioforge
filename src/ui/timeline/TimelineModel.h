#pragma once

#include "assets/AudioBufferCache.h"
#include "model/Project.h"

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace af::ui
{

/// A clip as the timeline draws it: times in seconds (display only, never written back).
struct TimelineClip
{
    double start = 0.0;
    double length = 0.0;
    juce::String name;
    assets::AssetKey asset;
    core::FrameCount sourceIn = 0; ///< First source frame shown, at the asset's rate.
    core::SampleRate sourceRate = 0;
    bool reverse = false;

    [[nodiscard]] double end() const noexcept { return start + length; }
};

struct TimelineLayer
{
    juce::String name;
    std::vector<TimelineClip> clips; ///< Sorted by start.
    double maxClipLength = 0.0;      ///< Longest clip: bounds the search for visible clips.
    bool mute = false;
    bool solo = false;
};

/// Snapshot of one event for drawing: layers top to bottom.
struct TimelineModel
{
    std::vector<TimelineLayer> layers;
    double lengthSeconds = 0.0; ///< End of the last clip.

    [[nodiscard]] static TimelineModel fromProject(const model::Project& project,
                                                   model::EventId event);
};

} // namespace af::ui
