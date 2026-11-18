#pragma once

#include "assets/AudioBufferCache.h"
#include "model/Project.h"

namespace af::app
{

/// A project with one 4-second 440 Hz mono tone (-12 dBFS, 10 ms S-curve fades), its audio
/// inserted into `cache`. Playback loops it to check a device through the whole engine path.
struct TestToneProject
{
    model::Project project;
    model::EventId event;
};

[[nodiscard]] TestToneProject makeTestTone(assets::AudioBufferCache& cache);

} // namespace af::app
