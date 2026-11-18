#include "core/PluralRules.h"

#include <catch2/catch_test_macros.hpp>

using af::core::pluralCategory;
using af::core::PluralCategory;

TEST_CASE("English plural rule", "[core][i18n]")
{
    REQUIRE(pluralCategory("en", 1) == PluralCategory::one);
    REQUIRE(pluralCategory("en", 0) == PluralCategory::other);
    REQUIRE(pluralCategory("en", 2) == PluralCategory::other);
    REQUIRE(pluralCategory("en", 21) == PluralCategory::other);
}

TEST_CASE("Ukrainian plural rule (1 fail, 2 faily, 5 failiv)", "[core][i18n]")
{
    for (const auto n : {1, 21, 31, 101, 1001})
    {
        CAPTURE(n);
        REQUIRE(pluralCategory("uk", n) == PluralCategory::one);
    }
    for (const auto n : {2, 3, 4, 22, 23, 24, 102, 1004})
    {
        CAPTURE(n);
        REQUIRE(pluralCategory("uk", n) == PluralCategory::few);
    }
    for (const auto n : {0, 5, 9, 10, 11, 12, 13, 14, 15, 20, 25, 111, 112, 114, 1000})
    {
        CAPTURE(n);
        REQUIRE(pluralCategory("uk", n) == PluralCategory::many);
    }
}

TEST_CASE("Russian uses the same integer rule as Ukrainian", "[core][i18n]")
{
    for (std::int64_t n = 0; n <= 300; ++n)
    {
        CAPTURE(n);
        REQUIRE(pluralCategory("ru", n) == pluralCategory("uk", n));
    }
}

TEST_CASE("Region suffixes and unknown languages", "[core][i18n]")
{
    REQUIRE(pluralCategory("uk-UA", 3) == PluralCategory::few);
    REQUIRE(pluralCategory("uk_UA", 5) == PluralCategory::many);
    REQUIRE(pluralCategory("xx", 1) == PluralCategory::one);
    REQUIRE(pluralCategory("xx", 3) == PluralCategory::other);
    REQUIRE(af::core::toString(PluralCategory::few) == "few");
}
