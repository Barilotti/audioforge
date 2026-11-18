#include "core/Uuid.h"

#include <chrono>
#include <mutex>
#include <random>

namespace af::core
{

namespace
{

constexpr std::string_view hexDigits = "0123456789abcdef";

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

constexpr bool isDashPosition(std::size_t index) noexcept
{
    return index == 8 || index == 13 || index == 18 || index == 23;
}

} // namespace

Uuid Uuid::generate()
{
    // One generator for the process, seeded from the OS. Id generation is rare (user actions),
    // so a mutex is cheaper than per-thread engines with their own seeding.
    static std::mutex mutex;
    static std::mt19937_64 engine{[]
                                  {
                                      std::random_device device;
                                      std::seed_seq seed{device(), device(), device(),
                                                         device(), device(), device()};
                                      return std::mt19937_64{seed};
                                  }()};

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());

    std::uint64_t randomA = 0;
    std::uint64_t randomB = 0;
    {
        const std::scoped_lock lock{mutex};
        randomA = engine();
        randomB = engine();
    }

    Bytes b{};
    for (std::size_t i = 0; i < 6; ++i) // 48-bit Unix time in milliseconds, big-endian
    {
        b[i] = static_cast<std::uint8_t>(millis >> (8 * (5 - i)));
    }
    for (std::size_t i = 6; i < 8; ++i)
    {
        b[i] = static_cast<std::uint8_t>(randomA >> (8 * (i - 6)));
    }
    for (std::size_t i = 8; i < 16; ++i)
    {
        b[i] = static_cast<std::uint8_t>(randomB >> (8 * (i - 8)));
    }
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0F) | 0x70); // version 7
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3F) | 0x80); // RFC 9562 variant
    return Uuid{b};
}

std::optional<Uuid> Uuid::parse(std::string_view text) noexcept
{
    if (text.size() != 36)
    {
        return std::nullopt;
    }

    Bytes b{};
    std::size_t byteIndex = 0;
    for (std::size_t i = 0; i < text.size();)
    {
        if (isDashPosition(i))
        {
            if (text[i] != '-')
            {
                return std::nullopt;
            }
            ++i;
            continue;
        }
        const int high = hexValue(text[i]);
        const int low = hexValue(text[i + 1]);
        if (high < 0 || low < 0 || isDashPosition(i + 1))
        {
            return std::nullopt;
        }
        b[byteIndex++] = static_cast<std::uint8_t>((high << 4) | low);
        i += 2;
    }
    return Uuid{b};
}

std::string Uuid::toString() const
{
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes_.size(); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
        {
            out.push_back('-');
        }
        out.push_back(hexDigits[bytes_[i] >> 4]);
        out.push_back(hexDigits[bytes_[i] & 0x0F]);
    }
    return out;
}

} // namespace af::core
