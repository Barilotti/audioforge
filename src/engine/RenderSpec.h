#pragma once

#include "core/TimeTicks.h"

#include <compare>

namespace af::engine
{

/// Rendering quality (architecture v0.2, section 4.5). One render code path; only the
/// parameters listed in the table of section 4.5 differ.
enum class RenderQuality
{
    draft,    ///< Emergency mode for heavy projects: cubic resampling.
    asExport, ///< Default preview: same algorithms as the final export.
    final,    ///< Export.
};

/// What a graph is compiled for. A graph only plays on a device with the same sample rate and
/// a block size not above maxBlockFrames.
struct RenderSpec
{
    core::SampleRate sampleRate = 48'000;
    int maxBlockFrames = 512;
    RenderQuality quality = RenderQuality::asExport;

    friend bool operator==(const RenderSpec&, const RenderSpec&) = default;
};

} // namespace af::engine
