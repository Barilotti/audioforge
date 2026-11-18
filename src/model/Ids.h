#pragma once

#include "core/Uuid.h"

#include <compare>
#include <cstddef>
#include <functional>

namespace af::model
{

/// A typed entity id. The tag makes a ClipId and a LayerId different types, so one cannot be
/// passed where the other is expected. All ids are UUIDs (architecture v0.2, section 1.3);
/// references between entities always go through ids, never through indices.
template <typename Tag>
struct Id
{
    core::Uuid uuid;

    [[nodiscard]] static Id generate() { return Id{core::Uuid::generate()}; }

    [[nodiscard]] constexpr bool isNil() const noexcept { return uuid.isNil(); }

    constexpr auto operator<=>(const Id&) const noexcept = default;
};

using ProjectId = Id<struct ProjectTag>;
using AssetId = Id<struct AssetTag>;
using EventId = Id<struct EventTag>;
using LayerId = Id<struct LayerTag>;
using ClipId = Id<struct ClipTag>;

} // namespace af::model

template <typename Tag>
struct std::hash<af::model::Id<Tag>>
{
    std::size_t operator()(const af::model::Id<Tag>& id) const noexcept
    {
        return std::hash<af::core::Uuid>{}(id.uuid);
    }
};
