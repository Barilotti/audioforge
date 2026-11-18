#include "core/Version.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("Version string has the form major.minor.patch", "[core][version]")
{
    const std::string version{af::core::versionString()};

    REQUIRE(!version.empty());
    int dots = 0;
    for (const char c : version)
    {
        if (c == '.')
        {
            ++dots;
        }
        else
        {
            REQUIRE((c >= '0' && c <= '9'));
        }
    }
    REQUIRE(dots == 2);
}

TEST_CASE("Build description starts with the version", "[core][version]")
{
    const std::string description{af::core::buildDescription()};
    const std::string version{af::core::versionString()};

    REQUIRE(description.starts_with(version + " ("));
    REQUIRE(description.find(std::string{af::core::gitCommit()}) != std::string::npos);
}

TEST_CASE("JUCE version is reported", "[core][version]")
{
    const std::string juce{af::core::juceVersionString()};
    REQUIRE(juce.starts_with("JUCE v8."));
}
