#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace af::core
{

/// 128-bit identifier for every model entity (architecture v0.2, section 1.3).
/// New ids are UUID version 7: time-ordered, so ids sort roughly by creation time, with
/// 74 random bits. The nil UUID (all zeros) means "no id".
class Uuid
{
public:
    using Bytes = std::array<std::uint8_t, 16>;

    constexpr Uuid() noexcept = default;
    constexpr explicit Uuid(const Bytes& bytes) noexcept
        : bytes_(bytes)
    {
    }

    /// Creates a new UUID v7. Thread-safe. Not real-time safe.
    [[nodiscard]] static Uuid generate();

    /// Parses the canonical form "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (case-insensitive).
    [[nodiscard]] static std::optional<Uuid> parse(std::string_view text) noexcept;

    /// Canonical lower-case form.
    [[nodiscard]] std::string toString() const;

    [[nodiscard]] constexpr bool isNil() const noexcept { return bytes_ == Bytes{}; }
    [[nodiscard]] constexpr int version() const noexcept { return bytes_[6] >> 4; }
    [[nodiscard]] constexpr const Bytes& bytes() const noexcept { return bytes_; }

    constexpr auto operator<=>(const Uuid&) const noexcept = default;

private:
    Bytes bytes_{};
};

} // namespace af::core

template <>
struct std::hash<af::core::Uuid>
{
    std::size_t operator()(const af::core::Uuid& id) const noexcept
    {
        // The random bits of a v7 UUID are already well distributed.
        std::uint64_t h = 0;
        for (std::size_t i = 8; i < 16; ++i)
        {
            h = (h << 8) | id.bytes()[i];
        }
        return static_cast<std::size_t>(h ^ (h >> 32));
    }
};
