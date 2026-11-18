#pragma once

#include "core/TimeTicks.h"
#include "model/Asset.h"
#include "model/Event.h"
#include "model/Ids.h"
#include "model/Node.h"

#include <optional>
#include <vector>

namespace af::model
{

struct ProjectSettings
{
    /// Sample rate offered for new renders and used when no device is open.
    core::SampleRate defaultSampleRate = 48'000;

    friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};

/// The root of the document (architecture v0.2, section 1.3). A Project value is one version of
/// the document; every edit produces a new Project that shares the unchanged nodes.
struct Project
{
    ProjectId id;
    ProjectSettings settings;
    std::vector<AssetPtr> assets; ///< In import order, which keeps the file format stable.
    std::vector<EventPtr> events;

    /// The asset with this id, or nullptr.
    [[nodiscard]] AssetPtr findAsset(AssetId asset) const noexcept;

    /// The event with this id, or nullptr.
    [[nodiscard]] EventPtr findEvent(EventId event) const noexcept;
};

using ProjectPtr = NodePtr<Project>;

/// A new empty project with a fresh id and one empty Event.
[[nodiscard]] ProjectPtr makeEmptyProject();

/// Length of the clip on the timeline: its source frames at the asset's sample rate, rounded to
/// the nearest tick (exact for standard rates).
[[nodiscard]] core::TimeTicks clipTimelineLength(const Clip& clip, const AssetInfo& asset) noexcept;

/// End of the last clip in the event, or zero for an empty event. Clips that refer to a missing
/// asset are ignored; validate() reports them.
[[nodiscard]] core::TimeTicks eventEnd(const Event& event, const Project& project) noexcept;

} // namespace af::model
