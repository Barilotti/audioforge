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
    meter_.prepare(sampleRate);
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
    publishedEnd_.store(0, std::memory_order_release);
}

bool AudioEngine::transport(TransportCommand command) noexcept
{
    if (command.kind == TransportCommand::Kind::seek)
    {
        command.frame = std::max<core::FrameCount>(command.frame, 0);
    }
    return commands_.tryPush(command);
}

bool AudioEngine::popEvent(EngineEvent& event) noexcept
{
    return events_.tryPop(event);
}

EngineStatus AudioEngine::status() const noexcept
{
    EngineStatus s;
    s.appliedGeneration = appliedGeneration_.load(std::memory_order_acquire);
    s.position = publishedPosition_.load(std::memory_order_acquire);
    s.playing = publishedPlaying_.load(std::memory_order_acquire);
    s.endFrame = publishedEnd_.load(std::memory_order_acquire);
    s.appliedCommands = publishedApplied_.load(std::memory_order_acquire);
    s.deferredSwaps = deferredSwaps_.load(std::memory_order_relaxed);
    s.rejectedGraphs = rejectedGraphs_.load(std::memory_order_relaxed);
    s.droppedEvents = droppedEvents_.load(std::memory_order_relaxed);
    return s;
}

void AudioEngine::applyCommands() noexcept
{
    TransportCommand command;
    while (commands_.tryPop(command))
    {
        switch (command.kind)
        {
        case TransportCommand::Kind::play:
            playing_ = true;
            break;
        case TransportCommand::Kind::pause:
            playing_ = false;
            break;
        case TransportCommand::Kind::seek:
            position_ = command.frame;
            break;
        }
        ++applied_;
    }
}

void AudioEngine::checkEnd() noexcept
{
    if (!playing_ || current_ == nullptr || position_ < current_->endFrame())
    {
        return;
    }
    playing_ = false;
    EngineEvent event{EngineEvent::Kind::endReached, position_, applied_};
    if (!events_.tryPush(event))
    {
        droppedEvents_.fetch_add(1, std::memory_order_relaxed);
    }
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

    if (current_ != nullptr && next->needsCrossfade() && playing_)
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
    publishedEnd_.store(next->endFrame(), std::memory_order_release);
}

void AudioEngine::renderInto(float* left, float* right, int frames) noexcept
{
    const auto count = static_cast<std::size_t>(frames);
    if (current_ == nullptr || !playing_)
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
    applyCommands();
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
        meter_.measure(left, right, n);

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
    checkEnd();
    publishedPosition_.store(position_, std::memory_order_release);
    publishedPlaying_.store(playing_, std::memory_order_release);
    publishedApplied_.store(applied_, std::memory_order_release);
}

} // namespace af::engine
