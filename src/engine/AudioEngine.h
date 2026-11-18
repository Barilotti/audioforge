#pragma once

#include "core/rt/Mailbox.h"
#include "core/rt/Realtime.h"
#include "core/rt/RetireQueue.h"
#include "core/rt/SpscQueue.h"
#include "engine/MasterMeter.h"
#include "engine/RenderGraph.h"
#include "engine/RenderSpec.h"
#include "engine/Transport.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace af::engine
{

struct EngineStatus
{
    std::uint64_t appliedGeneration = 0; ///< Generation of the graph that is audible (0: none).
    core::FrameCount position = 0;       ///< Timeline frame of the next block.
    bool playing = false;                ///< As applied by the audio thread.
    core::FrameCount endFrame = 0;       ///< End of the audible graph (0: none or empty).
    std::uint64_t appliedCommands = 0;   ///< Transport commands the audio thread has applied.
    std::uint64_t deferredSwaps = 0;  ///< Blocks where a new graph waited for a RetireQueue slot.
    std::uint64_t rejectedGraphs = 0; ///< Graphs compiled for another sample rate or block size.
    std::uint64_t droppedEvents = 0;  ///< Events lost because the event queue was full.
};

/// The real-time player (architecture v0.2, section 4.2). Owns the graph hand-over:
/// message thread -> Mailbox -> audio thread -> RetireQueue -> message thread -> delete.
///
/// Threads: publish(), collectRetired(), prepare(), deviceStopped(), transport(), popEvent()
/// and meter().read() on the message thread; process() on the audio thread; status() from any
/// thread.
/// prepare() and deviceStopped() require that process() is not running (JUCE guarantees this
/// in audioDeviceAboutToStart() and audioDeviceStopped()).
class AudioEngine
{
public:
    static constexpr int crossfadeFrames = 256;
    static constexpr std::size_t commandQueueSize = 256;
    static constexpr std::size_t eventQueueSize = 256;

    AudioEngine() = default;
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    /// Device format: graphs for any other spec are rejected. Allocates scratch buffers.
    void prepare(core::SampleRate sampleRate, int maxBlockFrames);

    /// Hands a graph to the audio thread; latest wins. A graph that was published earlier and
    /// not yet taken is deleted here. The caller also runs collectRetired() after publishing and
    /// on a timer (architecture v0.2, section 4.2).
    void publish(std::unique_ptr<RenderGraph> graph);

    /// Deletes graphs the audio thread has finished with. Call regularly (30 Hz).
    std::size_t collectRetired();

    /// Takes back and deletes every graph: current, crossfading, pending and retired.
    /// Afterwards the engine outputs silence until a new graph is published.
    void deviceStopped();

    /// Queues a transport command, applied at the start of the next block. Returns false when
    /// the queue is full; the caller keeps the command and tries again (app::Transport does).
    [[nodiscard]] bool transport(TransportCommand command) noexcept AF_NONBLOCKING;

    /// Takes the next event from the audio thread, if any.
    [[nodiscard]] bool popEvent(EngineEvent& event) noexcept AF_NONBLOCKING;

    [[nodiscard]] MasterMeter& meter() noexcept { return meter_; }

    [[nodiscard]] EngineStatus status() const noexcept AF_NONBLOCKING;

    /// Audio callback. `outputs` may contain null pointers for inactive channels. Channel 0 and
    /// 1 get left and right; a single output gets the mono sum; further channels get silence.
    void process(float* const* outputs, int outputCount, int frames) noexcept AF_NONBLOCKING;

private:
    void applyCommands() noexcept AF_NONBLOCKING;
    void acquirePendingGraph() noexcept AF_NONBLOCKING;
    void checkEnd() noexcept AF_NONBLOCKING;
    void renderInto(float* left, float* right, int frames) noexcept AF_NONBLOCKING;

    core::rt::Mailbox<RenderGraph> mailbox_;
    core::rt::RetireQueue<RenderGraph, 64> retired_;
    core::rt::SpscQueue<TransportCommand, commandQueueSize> commands_;
    core::rt::SpscQueue<EngineEvent, eventQueueSize> events_;
    MasterMeter meter_;

    // Audio-thread state (also touched by prepare/deviceStopped while the callback is stopped).
    RenderGraph* current_ = nullptr;
    RenderGraph* fadingOut_ = nullptr;
    int crossfadePosition_ = 0;
    core::FrameCount position_ = 0;
    bool playing_ = false;
    std::uint64_t applied_ = 0;
    core::SampleRate sampleRate_ = 0;
    int maxBlockFrames_ = 0;
    std::vector<float> mainLeft_;
    std::vector<float> mainRight_;
    std::vector<float> oldLeft_;
    std::vector<float> oldRight_;

    // Shared with other threads.
    std::atomic<bool> publishedPlaying_{false};
    std::atomic<core::FrameCount> publishedEnd_{0};
    std::atomic<std::uint64_t> publishedApplied_{0};
    std::atomic<std::uint64_t> appliedGeneration_{0};
    std::atomic<core::FrameCount> publishedPosition_{0};
    std::atomic<std::uint64_t> deferredSwaps_{0};
    std::atomic<std::uint64_t> rejectedGraphs_{0};
    std::atomic<std::uint64_t> droppedEvents_{0};
};

} // namespace af::engine
