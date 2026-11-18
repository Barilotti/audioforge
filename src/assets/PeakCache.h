#pragma once

#include "assets/ContentHash.h"
#include "assets/PeakData.h"
#include "core/Result.h"

#include <filesystem>

namespace af::assets
{

/// The global on-disk peak cache (architecture v0.2, 5.5):
/// `<directory>/<xxh3-128 hex>.afpk`, one file per distinct file content, shared by all
/// projects. Files are written atomically and carry a version and a checksum; a missing,
/// outdated or damaged file reads as a miss and is rewritten. The folder may be deleted at any
/// time. Thread-safe: concurrent readers and writers of the same key are fine.
class PeakCache
{
public:
    static constexpr std::uint32_t formatVersion = 1;

    explicit PeakCache(std::filesystem::path directory);

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    [[nodiscard]] std::filesystem::path fileFor(const ContentHash& hash) const;

    /// The cached peaks, or nullptr on a miss.
    [[nodiscard]] PeakDataPtr load(const ContentHash& hash) const;

    core::Result<void> store(const ContentHash& hash, const PeakData& peaks) const;

private:
    std::filesystem::path directory_;
};

} // namespace af::assets
