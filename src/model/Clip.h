#pragma once

#include "core/TimeTicks.h"
#include "model/Ids.h"
#include "model/Node.h"

namespace af::model
{

enum class FadeCurve
{
    linear,
    equalPower,
    exponential,
    sCurve,
};

struct Fade
{
    core::TimeTicks length{}; ///< Zero means no fade.
    FadeCurve curve = FadeCurve::equalPower;

    friend bool operator==(const Fade&, const Fade&) = default;
};

/// A piece of source material inside a layer (architecture v0.2, section 1.3).
///
/// The clip plays the source frames [sourceInFrames, sourceOutFrames) of its asset, starting at
/// `position` on the Event timeline. Source positions are frames of the file at its own sample
/// rate, so they stay exact for any rate; the length on the timeline follows from them.
struct Clip
{
    ClipId id;
    AssetId asset;
    core::TimeTicks position{};
    core::FrameCount sourceInFrames = 0;
    core::FrameCount sourceOutFrames = 0;
    double gainDb = 0.0;
    bool reverse = false;
    Fade fadeIn;
    Fade fadeOut;

    [[nodiscard]] core::FrameCount sourceLengthFrames() const noexcept
    {
        return sourceOutFrames - sourceInFrames;
    }

    friend bool operator==(const Clip&, const Clip&) = default;
};

using ClipPtr = NodePtr<Clip>;

} // namespace af::model
