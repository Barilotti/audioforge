#pragma once

#include "model/Clip.h"
#include "model/Ids.h"
#include "model/Node.h"

#include <cstdint>
#include <string>
#include <vector>

namespace af::model
{

/// A row on the timeline: the channel strip that the user calls a layer (architecture v0.2,
/// section 1.3). Clips may overlap; overlapping clips are summed.
struct Layer
{
    LayerId id;
    std::string name;
    std::uint32_t colourRgb = 0xE0A030; ///< 0xRRGGBB.
    double gainDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
    std::vector<ClipPtr> clips;
};

using LayerPtr = NodePtr<Layer>;

} // namespace af::model
