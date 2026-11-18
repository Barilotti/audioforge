#include "core/PluralRules.h"

namespace af::core
{

namespace
{

std::string_view baseLanguage(std::string_view language) noexcept
{
    const auto separator = language.find_first_of("-_");
    return separator == std::string_view::npos ? language : language.substr(0, separator);
}

// CLDR rule shared by Ukrainian and Russian for integers:
//   one:  n % 10 = 1 and n % 100 != 11
//   few:  n % 10 = 2..4 and n % 100 != 12..14
//   many: everything else
PluralCategory eastSlavic(std::int64_t count) noexcept
{
    const auto n = count < 0 ? -count : count;
    const auto mod10 = n % 10;
    const auto mod100 = n % 100;
    if (mod10 == 1 && mod100 != 11)
    {
        return PluralCategory::one;
    }
    if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
    {
        return PluralCategory::few;
    }
    return PluralCategory::many;
}

} // namespace

PluralCategory pluralCategory(std::string_view language, std::int64_t count) noexcept
{
    const auto base = baseLanguage(language);
    if (base == "uk" || base == "ru")
    {
        return eastSlavic(count);
    }
    return count == 1 || count == -1 ? PluralCategory::one : PluralCategory::other;
}

std::string_view toString(PluralCategory category) noexcept
{
    switch (category)
    {
    case PluralCategory::zero:
        return "zero";
    case PluralCategory::one:
        return "one";
    case PluralCategory::two:
        return "two";
    case PluralCategory::few:
        return "few";
    case PluralCategory::many:
        return "many";
    case PluralCategory::other:
        return "other";
    }
    return "other";
}

} // namespace af::core
