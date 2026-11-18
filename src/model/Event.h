#pragma once

#include "model/Ids.h"
#include "model/Layer.h"
#include "model/Node.h"

#include <string>
#include <vector>

namespace af::model
{

/// One sound being designed: its layers and master bus. Phase 1 shows one Event in the UI, but
/// the model already holds many. Groups, markers and the loop region come in later stages.
struct Event
{
    EventId id;
    std::string name;
    std::vector<LayerPtr> layers; ///< Top to bottom.
    double masterGainDb = 0.0;
};

using EventPtr = NodePtr<Event>;

} // namespace af::model
