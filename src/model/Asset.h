#pragma once

#include "core/TimeTicks.h"
#include "model/Ids.h"
#include "model/Node.h"

#include <string>

namespace af::model
{

/// Audio properties of an asset, read when it was imported and cached in the project, so a
/// project with missing files still knows how long its clips are.
struct AssetInfo
{
    core::SampleRate sampleRate = 0;
    int channels = 0;
    core::FrameCount frames = 0;

    friend bool operator==(const AssetInfo&, const AssetInfo&) = default;
};

/// A reference to an audio file. Assets are references, never copies (architecture v0.2, 5.5).
/// Paths are UTF-8. The file fingerprint is added together with the asset subsystem.
struct AssetRef
{
    AssetId id;
    std::string absolutePath;
    std::string projectRelativePath; ///< Empty when the project has not been saved yet.
    AssetInfo info;

    friend bool operator==(const AssetRef&, const AssetRef&) = default;
};

using AssetPtr = NodePtr<AssetRef>;

} // namespace af::model
