#pragma once

#include <cstdint>
#include <string_view>

namespace af::core
{

/// CLDR plural categories (https://cldr.unicode.org, "Language Plural Rules").
/// Messages with counts are translated per category instead of being glued together, because
/// Ukrainian and Russian need three forms for whole numbers (one / few / many) where English
/// needs two (one / other). See ADR-0007.
enum class PluralCategory
{
    zero,
    one,
    two,
    few,
    many,
    other,
};

/// Plural category of a whole-number count for a language ("en", "uk", "ru"; region suffixes
/// such as "uk-UA" are accepted). Unknown languages use the English rule.
[[nodiscard]] PluralCategory pluralCategory(std::string_view language, std::int64_t count) noexcept;

[[nodiscard]] std::string_view toString(PluralCategory category) noexcept;

} // namespace af::core
