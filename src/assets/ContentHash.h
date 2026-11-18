#pragma once

#include "core/Result.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace af::assets
{

/// XXH3-128 of a file's bytes (architecture v0.2, 5.5). Keys the global peak cache, so renaming
/// or moving a file does not recompute its peaks, and later identifies moved files for Relink.
struct ContentHash
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    /// 32 lowercase hex digits, high part first.
    [[nodiscard]] std::string toHex() const;
    [[nodiscard]] static std::optional<ContentHash> fromHex(std::string_view hex);

    friend bool operator==(const ContentHash&, const ContentHash&) = default;
};

/// Hashes the whole file. Returns a cancelled error as soon as `cancel` becomes true.
[[nodiscard]] core::Result<ContentHash> hashFile(const std::filesystem::path& file,
                                                 const std::atomic<bool>* cancel = nullptr);

/// Hashes bytes in memory (used for the integrity check of cache files).
[[nodiscard]] std::uint64_t hash64(const void* data, std::size_t size) noexcept;

} // namespace af::assets
