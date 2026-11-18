#include "engine/RenderGraph.h"

#include <algorithm>
#include <cstddef>

namespace af::engine
{

float ClipVoice::sourceSample(int channel, core::FrameCount index) const noexcept
{
    if (index < 0 || index >= sourceLength)
    {
        return 0.0f; // outside the clip's source range: silence, never neighbouring material
    }
    const auto frame = reverse ? sourceIn + sourceLength - 1 - index : sourceIn + index;
    return audio->channel(channel)[frame];
}

void ClipVoice::sourceFrame(core::FrameCount n, float& left, float& right) const noexcept
{
    const bool stereo = sourceChannels > 1;
    if (sourceRate == renderRate)
    {
        left = sourceSample(0, n);
        right = stereo ? sourceSample(1, n) : left;
        return;
    }

    // Exact source position n * sourceRate / renderRate as integer part plus fraction.
    const core::FrameCount numerator = n * sourceRate;
    const core::FrameCount base = numerator / renderRate;
    const double frac =
        static_cast<double>(numerator % renderRate) / static_cast<double>(renderRate);

    float l = 0.0f;
    float r = 0.0f;
    if (kernel)
    {
        const int radius = kernel->radius();
        for (core::FrameCount j = base - radius + 1; j <= base + radius; ++j)
        {
            const float w = kernel->weight(static_cast<double>(base - j) + frac);
            l += w * sourceSample(0, j);
            if (stereo)
            {
                r += w * sourceSample(1, j);
            }
        }
    }
    else
    {
        // Draft quality: 4-point Catmull-Rom.
        const auto t = static_cast<float>(frac);
        const auto cubic = [&](int ch)
        {
            const float p0 = sourceSample(ch, base - 1);
            const float p1 = sourceSample(ch, base);
            const float p2 = sourceSample(ch, base + 1);
            const float p3 = sourceSample(ch, base + 2);
            return p1 + 0.5f * t *
                            (p2 - p0 +
                             t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 +
                                  t * (3.0f * (p1 - p2) + p3 - p0)));
        };
        l = cubic(0);
        r = stereo ? cubic(1) : 0.0f;
    }
    left = l;
    right = stereo ? r : l;
}

float ClipVoice::envelope(core::FrameCount n) const noexcept
{
    float g = gain;
    if (fadeInFrames > 0 && n < fadeInFrames)
    {
        g *= dsp::fadeGain(fadeInShape, static_cast<double>(n) / static_cast<double>(fadeInFrames));
    }
    const auto fromEnd = lengthFrames - n;
    if (fadeOutFrames > 0 && fromEnd < fadeOutFrames)
    {
        g *= dsp::fadeGain(fadeOutShape,
                           static_cast<double>(fromEnd) / static_cast<double>(fadeOutFrames));
    }
    return g;
}

void ClipVoice::render(core::FrameCount blockStart, int frames, float* left, float* right,
                       bool monoBus) const noexcept
{
    const auto from = std::max(blockStart, startFrame);
    const auto to = std::min(blockStart + frames, endFrame());
    for (auto frame = from; frame < to; ++frame)
    {
        const auto n = frame - startFrame;
        float l = 0.0f;
        float r = 0.0f;
        sourceFrame(n, l, r);
        const float g = envelope(n);
        const auto i = static_cast<std::size_t>(frame - blockStart);
        if (monoBus)
        {
            left[i] += l * g;
        }
        else
        {
            left[i] += l * g;
            right[i] += r * g;
        }
    }
}

RenderGraph::RenderGraph(const RenderSpec& spec, std::uint64_t generation, Contents contents)
    : spec_(spec)
    , generation_(generation)
    , contents_(std::move(contents))
    , busLeft_(static_cast<std::size_t>(spec.maxBlockFrames))
    , busRight_(static_cast<std::size_t>(spec.maxBlockFrames))
{
}

void RenderGraph::render(core::FrameCount start, int frames, float* left, float* right) noexcept
{
    const auto count = static_cast<std::size_t>(frames);
    std::fill_n(left, count, 0.0f);
    std::fill_n(right, count, 0.0f);

    for (const auto& layer : contents_.layers)
    {
        if (!layer.audible)
        {
            continue;
        }
        std::fill_n(busLeft_.begin(), count, 0.0f);
        std::fill_n(busRight_.begin(), count, 0.0f);
        bool touched = false;
        for (const auto& clip : layer.clips)
        {
            if (clip.endFrame() <= start || clip.startFrame >= start + frames)
            {
                continue;
            }
            clip.render(start, frames, busLeft_.data(), busRight_.data(), layer.monoBus);
            touched = true;
        }
        if (!touched)
        {
            continue;
        }
        const float* sourceRight = layer.monoBus ? busLeft_.data() : busRight_.data();
        for (std::size_t i = 0; i < count; ++i)
        {
            left[i] += busLeft_[i] * layer.gains.left;
            right[i] += sourceRight[i] * layer.gains.right;
        }
    }

    const float master = contents_.masterGain;
    for (std::size_t i = 0; i < count; ++i)
    {
        left[i] *= master;
        right[i] *= master;
    }
}

} // namespace af::engine
