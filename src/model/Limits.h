#pragma once

#include "core/TimeTicks.h"

namespace af::model::limits
{

/// Gain range for clip, layer and master gain, in dB. -96 dB is the bottom of the fader;
/// full silence is expressed with mute, not with an infinite attenuation.
inline constexpr double minGainDb = -96.0;
inline constexpr double maxGainDb = 24.0;

/// Pan: -1 is hard left, 0 centre, +1 hard right.
inline constexpr double minPan = -1.0;
inline constexpr double maxPan = 1.0;

/// Largest channel count of an asset.
inline constexpr int maxAssetChannels = 64;

/// Every clip must end within this distance from the Event start, and no asset may be longer
/// (architecture v0.2, section 5.3).
inline constexpr core::TimeTicks maxEventLength{core::TimeTicks::maxEventLength};

} // namespace af::model::limits
