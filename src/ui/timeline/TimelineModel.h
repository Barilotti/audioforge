#pragma once

#include "assets/AudioBufferCache.h"
#include "model/Project.h"

#include <juce_graphics/juce_graphics.h>

#include <optional>
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
    core::FrameCount sourceLength = 0;

    // Filled from the AudioBufferCache as the asset loads (TimelineView::refresh).
    assets::PeakDataPtr peaks;
    assets::DecodedAudioPtr audio; ///< For zoom levels finer than the finest peaks.
    bool failed = false;           ///< The asset could not be loaded: drawn as offline.

    [[nodiscard]] double end() const noexcept { return start + length; }
    [[nodiscard]] int channels() const noexcept;

    /// Min and max of `channel` over the timeline interval [t0, t1) in seconds, from the
    /// samples when a pixel covers fewer frames than the finest peak block and the audio is
    /// in memory, otherwise from the peaks. Returns nullopt where the clip has no data yet or
    /// the interval is outside the clip.
    [[nodiscard]] std::optional<assets::MinMax> columnRange(int channel, double t0,
                                                            double t1) const noexcept;
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

    /// Takes peaks and audio from the cache for clips that miss them. Returns true when
    /// anything changed (the view repaints).
    bool updateAssets(const assets::AudioBufferCache& cache);
    /// True while some clip waits for peaks or audio.
    [[nodiscard]] bool waitingForAssets() const noexcept;
};

} // namespace af::ui
