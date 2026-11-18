#include "engine/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace af::engine
{

AudioEngine::~AudioEngine()
{
    deviceStopped();
}

void AudioEngine::prepare(core::SampleRate sampleRate, int maxBlockFrames)
{
    sampleRate_ = sampleRate;
    maxBlockFrames_ = maxBlockFrames;
    const auto size = static_cast<std::size_t>(std::max(maxBlockFrames, 1));
    mainLeft_.assign(size, 0.0f);
    mainRight_.assign(size, 0.0f);
    oldLeft_.assign(size, 0.0f);
    oldRight_.assign(size, 0.0f);
}

void AudioEngine::publish(std::unique_ptr<RenderGraph> graph)
{
    auto stale = mailbox_.publish(std::move(graph));
    stale.reset(); // never taken by the audio thread, so deleting it here is safe
}

std::size_t AudioEngine::collectRetired()
{
    return retired_.collect();
}

void AudioEngine::deviceStopped()
{
    delete current_;
    current_ = nullptr;
    delete fadingOut_;
    fadingOut_ = nullptr;
    crossfadePosition_ = 0;
    mailbox_.reclaim().reset();
    retired_.collect();
    appliedGeneration_.store(0, std::memory_order_release);
}

void AudioEngine::setPlaying(bool playing) noexcept
{
    playing_.store(playing, std::memory_order_release);
}

void AudioEngine::seek(core::FrameCount frame) noexcept
{
    seekRequest_.store(std::max<core::FrameCount>(frame, 0), std::memory_order_release);
}

EngineStatus AudioEngine::status() const noexcept
{
    EngineStatus s;
    s.appliedGeneration = appliedGeneration_.load(std::memory_order_acquire);
    s.position = publishedPosition_.load(std::memory_order_acquire);
    s.playing = playing_.load(std::memory_order_acquire);
    s.deferredSwaps = deferredSwaps_.load(std::memory_order_relaxed);
    s.rejectedGraphs = rejectedGraphs_.load(std::memory_order_relaxed);
    return s;
}

void AudioEngine::acquirePendingGraph() noexcept
{
    if (fadingOut_ != nullptr || !mailbox_.hasPending())
    {
        return; // finish the running crossfade first
    }
    if (!retired_.canRetire())
    {
        // The message thread has not collected retired graphs: keep playing the current graph
        // rather than freeing memory here (architecture v0.2, section 4.2, overflow table).
        deferredSwaps_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    RenderGraph* next = mailbox_.take();
    if (next == nullptr)
    {
        return;
    }
    if (next->spec().sampleRate != sampleRate_ || next->spec().maxBlockFrames < maxBlockFrames_)
    {
        (void)retired_.retire(next); // a slot was checked above
        rejectedGraphs_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (current_ != nullptr && next->needsCrossfade() && playing_.load(std::memory_order_acquire))
    {
        fadingOut_ = current_;
        crossfadePosition_ = 0;
    }
    else if (current_ != nullptr)
    {
        (void)retired_.retire(current_);
    }
    current_ = next;
    appliedGeneration_.store(next->generation(), std::memory_order_release);
}

void AudioEngine::renderInto(float* left, float* right, int frames) noexcept
{
    const auto count = static_cast<std::size_t>(frames);
    if (current_ == nullptr || !playing_.load(std::memory_order_acquire))
    {
        if (fadingOut_ != nullptr && retired_.retire(fadingOut_))
        {
            fadingOut_ = nullptr; // stopped during a crossfade: nothing left to fade
        }
        std::fill_n(left, count, 0.0f);
        std::fill_n(right, count, 0.0f);
        return;
    }

    current_->render(position_, frames, left, right);

    if (fadingOut_ != nullptr)
    {
        fadingOut_->render(position_, frames, oldLeft_.data(), oldRight_.data());
        for (std::size_t i = 0; i < count && crossfadePosition_ < crossfadeFrames;
             ++i, ++crossfadePosition_)
        {
            const double t = (static_cast<double>(crossfadePosition_) + 0.5) / crossfadeFrames;
            const auto in = static_cast<float>(std::sin(t * std::numbers::pi / 2.0));
            const auto out = static_cast<float>(std::cos(t * std::numbers::pi / 2.0));
            left[i] = left[i] * in + oldLeft_[i] * out;
            right[i] = right[i] * in + oldRight_[i] * out;
        }
        if (crossfadePosition_ >= crossfadeFrames && retired_.retire(fadingOut_))
        {
            fadingOut_ = nullptr;
        }
    }
    position_ += frames;
}

void AudioEngine::process(float* const* outputs, int outputCount, int frames) noexcept
{
    const auto requested = seekRequest_.exchange(-1, std::memory_order_acq_rel);
    if (requested >= 0)
    {
        position_ = requested;
    }

    acquirePendingGraph();

    // Render in pieces no longer than the prepared block size.
    int done = 0;
    while (done < frames)
    {
        const int n = std::min(frames - done, std::max(maxBlockFrames_, 1));
        float* left = mainLeft_.data();
        float* right = mainRight_.data();
        if (mainLeft_.empty())
        {
            break; // not prepared
        }
        renderInto(left, right, n);

        for (int ch = 0; ch < outputCount; ++ch)
        {
            float* out = outputs[ch];
            if (out == nullptr)
            {
                continue;
            }
            out += done;
            if (outputCount == 1)
            {
                for (int i = 0; i < n; ++i)
                {
                    out[i] = 0.5f * (left[i] + right[i]);
                }
            }
            else if (ch < 2)
            {
                std::copy_n(ch == 0 ? left : right, n, out);
            }
            else
            {
                std::fill_n(out, n, 0.0f);
            }
        }
        done += n;
    }
    if (done < frames)
    {
        for (int ch = 0; ch < outputCount; ++ch)
        {
            if (outputs[ch] != nullptr)
            {
                std::fill_n(outputs[ch] + done, frames - done, 0.0f);
            }
        }
    }
    publishedPosition_.store(position_, std::memory_order_release);
}

} // namespace af::engine
