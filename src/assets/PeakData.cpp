#include "assets/PeakData.h"

#include <algorithm>

namespace af::assets
{

namespace
{

std::size_t blocksFor(core::FrameCount frames, core::FrameCount block) noexcept
{
    return frames <= 0 ? 0 : static_cast<std::size_t>((frames + block - 1) / block);
}

MinMax combine(MinMax a, MinMax b) noexcept
{
    return {std::min(a.min, b.min), std::max(a.max, b.max)};
}

} // namespace

PeakData::PeakData(core::SampleRate sampleRate, int channels, core::FrameCount frames)
    : sampleRate_(sampleRate)
    , channels_(std::max(channels, 0))
    , frames_(std::max<core::FrameCount>(frames, 0))
{
    std::size_t total = 0;
    for (int l = 0; l < levelCount; ++l)
    {
        const auto index = static_cast<std::size_t>(l);
        counts_[index] = blocksFor(frames_, blockFrames[index]);
        levelOffsets_[index] = total;
        total += counts_[index] * static_cast<std::size_t>(channels_);
    }
    peaks_.resize(total);
}

std::size_t PeakData::blockCount(int level) const noexcept
{
    return counts_[static_cast<std::size_t>(level)];
}

std::size_t PeakData::offset(int level, int channel) const noexcept
{
    const auto index = static_cast<std::size_t>(level);
    return levelOffsets_[index] + static_cast<std::size_t>(channel) * counts_[index];
}

std::span<const MinMax> PeakData::level(int level, int channel) const noexcept
{
    return {peaks_.data() + offset(level, channel), blockCount(level)};
}

std::span<MinMax> PeakData::writableLevel(int level, int channel) noexcept
{
    return {peaks_.data() + offset(level, channel), blockCount(level)};
}

MinMax PeakData::range(int channel, core::FrameCount begin, core::FrameCount end) const noexcept
{
    begin = std::clamp<core::FrameCount>(begin, 0, frames_);
    end = std::clamp<core::FrameCount>(end, 0, frames_);
    if (end <= begin || channel < 0 || channel >= channels_)
    {
        return {};
    }
    int chosen = 0;
    for (int l = levelCount - 1; l > 0; --l)
    {
        if (blockFrames[static_cast<std::size_t>(l)] <= end - begin)
        {
            chosen = l;
            break;
        }
    }
    const auto block = blockFrames[static_cast<std::size_t>(chosen)];
    const auto peaks = level(chosen, channel);
    const auto first = static_cast<std::size_t>(begin / block);
    const auto last = static_cast<std::size_t>((end - 1) / block);
    MinMax result = peaks[first];
    for (std::size_t i = first + 1; i <= last; ++i)
    {
        result = combine(result, peaks[i]);
    }
    return result;
}

void PeakData::buildCoarseLevels() noexcept
{
    for (int l = 1; l < levelCount; ++l)
    {
        for (int ch = 0; ch < channels_; ++ch)
        {
            const auto fine = level(l - 1, ch);
            auto coarse = writableLevel(l, ch);
            for (std::size_t i = 0; i < coarse.size(); ++i)
            {
                const auto from = i * static_cast<std::size_t>(levelRatio);
                const auto to = std::min(fine.size(), from + static_cast<std::size_t>(levelRatio));
                MinMax value = fine[from];
                for (std::size_t j = from + 1; j < to; ++j)
                {
                    value = combine(value, fine[j]);
                }
                coarse[i] = value;
            }
        }
    }
}

PeakBuilder::PeakBuilder(core::SampleRate sampleRate, int channels, core::FrameCount frames)
    : data_(std::make_shared<PeakData>(sampleRate, channels, frames))
{
}

void PeakBuilder::add(std::span<const float* const> channels, core::FrameCount frames) noexcept
{
    constexpr auto block = PeakData::blockFrames[0];
    frames = std::min(frames, data_->frames() - written_);
    if (frames <= 0)
    {
        return;
    }
    const int count = std::min(static_cast<int>(channels.size()), data_->channels());
    for (int ch = 0; ch < count; ++ch)
    {
        const float* samples = channels[static_cast<std::size_t>(ch)];
        auto peaks = data_->writableLevel(0, ch);
        for (core::FrameCount i = 0; i < frames; ++i)
        {
            const auto n = written_ + i;
            const float x = samples[i];
            auto& peak = peaks[static_cast<std::size_t>(n / block)];
            if (n % block == 0)
            {
                peak = {x, x};
            }
            else
            {
                peak.min = std::min(peak.min, x);
                peak.max = std::max(peak.max, x);
            }
        }
    }
    written_ += frames;
}

std::shared_ptr<PeakData> PeakBuilder::finish()
{
    data_->buildCoarseLevels();
    return std::move(data_);
}

} // namespace af::assets
