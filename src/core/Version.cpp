#include "core/Version.h"

#include <juce_core/juce_core.h>

#include <string>

namespace af::core
{

std::string_view juceVersionString()
{
    // JUCE stays a private dependency of core: only std types cross the module boundary.
    static const std::string version = juce::SystemStats::getJUCEVersion().toStdString();
    return version;
}

} // namespace af::core
