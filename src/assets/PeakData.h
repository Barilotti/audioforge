#pragma once

#include "core/TimeTicks.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace af::assets
{

struct MinMax
{
    float min = 0.0f;
    float max = 0.0f;

    friend bool operator==(const MinMax&, const MinMax&) = default;
};

/// Multi-resolution waveform peaks of one asset (architecture v0.2, 5.5): minimum and maximum of
/// every block of 256, 4096 and 65536 frames, per channel, at the file's own sample rate. The
/// last block of a level may be shorter. Immutable once built, so any thread may read it.
class PeakData
{
public:
    static constexpr int levelCount = 3;
    static constexpr std::array<core::FrameCount, levelCount> blockFrames{256, 4096, 65536};
    static constexpr core::FrameCount levelRatio = 16; ///< Blocks of a level per coarser block.

    /// Allocates zeroed peaks; throws std::bad_alloc when memory is exhausted.
    PeakData(core::SampleRate sampleRate, int channels, core::FrameCount frames);

    [[nodiscard]] core::SampleRate sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] core::FrameCount frames() const noexcept { return frames_; }
    [[nodiscard]] std::size_t blockCount(int level) const noexcept;
    [[nodiscard]] std::uint64_t bytes() const noexcept { return peaks_.size() * sizeof(MinMax); }

    [[nodiscard]] std::span<const MinMax> level(int level, int channel) const noexcept;
    [[nodiscard]] std::span<MinMax> writableLevel(int level, int channel) noexcept;

    /// Minimum and maximum of `channel` over frames [begin, end), clamped to the asset. Uses the
    /// coarsest level whose blocks are not longer than the range; blocks cut by the range edges
    /// count whole, so the result may include up to one block beyond each edge. Ranges shorter
    /// than 256 frames should be drawn from the samples instead.
    [[nodiscard]] MinMax range(int channel, core::FrameCount begin,
                               core::FrameCount end) const noexcept;

    /// Builds levels 1 and 2 from level 0.
    void buildCoarseLevels() noexcept;

    /// All peaks in storage order (level, then channel, then block). For the cache file.
    [[nodiscard]] std::span<const MinMax> storage() const noexcept { return peaks_; }
    [[nodiscard]] std::span<MinMax> writableStorage() noexcept { return peaks_; }

private:
    [[nodiscard]] std::size_t offset(int level, int channel) const noexcept;

    core::SampleRate sampleRate_;
    int channels_;
    core::FrameCount frames_;
    std::array<std::size_t, levelCount> counts_{};
    std::array<std::size_t, levelCount> levelOffsets_{};
    std::vector<MinMax> peaks_;
};

using PeakDataPtr = std::shared_ptr<const PeakData>;

/// Computes level 0 while a decoder delivers consecutive blocks of planar audio, so peaks cost
/// no extra pass over the file (architecture v0.2, 4.7).
class PeakBuilder
{
public:
    PeakBuilder(core::SampleRate sampleRate, int channels, core::FrameCount frames);

    /// `channels` holds one pointer per channel, each to `frames` consecutive samples that
    /// follow the samples added before.
    void add(std::span<const float* const> channels, core::FrameCount frames) noexcept;

    /// Builds the coarse levels and hands the peaks over. Call once, after the last add().
    [[nodiscard]] std::shared_ptr<PeakData> finish();

private:
    std::shared_ptr<PeakData> data_;
    core::FrameCount written_ = 0;
};

} // namespace af::assets
