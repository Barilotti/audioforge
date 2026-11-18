#pragma once

#include "core/Result.h"
#include "model/Project.h"

#include <vector>

namespace af::model
{

/// Checks every invariant of the document and returns all violations (empty when valid).
/// Each error has code invalidData; `subject` names the entity and field, for example
/// "clip 0193a5c2-....sourceOutFrames".
///
/// Invariants:
///  - no node pointer is null;
///  - every entity has a non-nil id, and no id appears twice in the project;
///  - sample rates are in the supported range; assets have 1...32 channels, at least one frame
///    and are at most 24 hours long;
///  - every clip refers to an asset of the project;
///  - 0 <= sourceIn < sourceOut <= asset frames;
///  - the clip starts at or after zero and ends within the 24-hour Event limit;
///  - fade lengths are not negative and fit into the clip together;
///  - gains are finite and within [-96, +24] dB, pans within [-1, +1].
[[nodiscard]] std::vector<core::Error> findInvariantViolations(const Project& project);

/// Same checks; returns the first violation.
[[nodiscard]] core::Result<void> validate(const Project& project);

} // namespace af::model
