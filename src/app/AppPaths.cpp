#include "app/AppPaths.h"

#include <juce_core/juce_core.h>

#include <cstdlib>

namespace af::app
{

namespace
{

std::filesystem::path toPath(const juce::File& file)
{
    return std::filesystem::path{file.getFullPathName().toWideCharPointer()};
}

} // namespace

AppPaths AppPaths::forCurrentUser()
{
    AppPaths paths;

    const auto overrideRoot = juce::SystemStats::getEnvironmentVariable("AUDIOFORGE_DATA_DIR", {});
    if (overrideRoot.isNotEmpty())
    {
        const auto root = toPath(juce::File{overrideRoot});
        paths.localData = root;
        paths.settings = root / "Settings";
    }
    else
    {
#if JUCE_WINDOWS
        paths.localData =
            toPath(juce::File::getSpecialLocation(juce::File::windowsLocalAppData)) / "AudioForge";
        paths.settings =
            toPath(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)) /
            "AudioForge";
#else
        paths.localData =
            toPath(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)) /
            "AudioForge";
        paths.settings = paths.localData / "Settings";
#endif
    }

    paths.logs = paths.localData / "Logs";
    paths.crashDumps = paths.localData / "Crashes";
    paths.cache = paths.localData / "Cache";
    return paths;
}

} // namespace af::app
