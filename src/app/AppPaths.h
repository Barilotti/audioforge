#pragma once

#include <filesystem>

namespace af::app
{

/// Per-user folders of AudioForge.
///
/// Windows: machine-local data in %LOCALAPPDATA%\AudioForge (logs, crash dumps, caches),
/// roaming settings in %APPDATA%\AudioForge. Other platforms (development only): everything
/// under the JUCE user application data folder.
///
/// The environment variable AUDIOFORGE_DATA_DIR overrides the root of all folders; tests use it
/// to keep their files out of the user's profile.
struct AppPaths
{
    std::filesystem::path localData;
    std::filesystem::path settings;
    std::filesystem::path logs;
    std::filesystem::path crashDumps;
    std::filesystem::path cache;

    [[nodiscard]] static AppPaths forCurrentUser();
};

} // namespace af::app
