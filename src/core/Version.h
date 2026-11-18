#pragma once

#include <string_view>

namespace af::core
{

/// Application version, e.g. "0.1.0".
[[nodiscard]] std::string_view versionString() noexcept;

/// Short git commit hash of the build, or "unknown".
[[nodiscard]] std::string_view gitCommit() noexcept;

/// Human-readable build description, e.g. "0.1.0 (a1b2c3d4e5, Debug)".
[[nodiscard]] std::string_view buildDescription() noexcept;

/// Version of the JUCE framework the build uses, e.g. "JUCE v8.0.17".
[[nodiscard]] std::string_view juceVersionString();

} // namespace af::core
