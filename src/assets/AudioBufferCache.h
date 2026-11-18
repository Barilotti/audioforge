#pragma once

#include "assets/AudioDecoder.h"
#include "assets/AudioFileInfo.h"
#include "assets/MemoryBudget.h"
#include "assets/PeakCache.h"
#include "assets/PeakData.h"
#include "core/Result.h"
#include "core/TimeTicks.h"
#include "core/Uuid.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace af::assets
{

/// Identifies an asset in the cache (the model's AssetId, which this module cannot see).
using AssetKey = core::Uuid;

/// A fully decoded asset: planar float32 at the file's own sample rate. Immutable once
/// published, so the audio thread can read it without locks.
class DecodedAudio
{
public:
    /// Allocates zeroed storage; throws std::bad_alloc when memory is exhausted.
    DecodedAudio(core::SampleRate sampleRate, int channels, core::FrameCount frames);

    [[nodiscard]] core::SampleRate sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] core::FrameCount frames() const noexcept { return frames_; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return samples_.size() * sizeof(float); }

    [[nodiscard]] const float* channel(int index) const noexcept
    {
        return samples_.data() +
               static_cast<std::size_t>(index) * static_cast<std::size_t>(frames_);
    }

    /// Write access for the decoder, before the buffer is published.
    [[nodiscard]] float* writableChannel(int index) noexcept
    {
        return samples_.data() +
               static_cast<std::size_t>(index) * static_cast<std::size_t>(frames_);
    }

private:
    core::SampleRate sampleRate_;
    int channels_;
    core::FrameCount frames_;
    std::vector<float> samples_;
};

using DecodedAudioPtr = std::shared_ptr<const DecodedAudio>;

/// Life cycle of an asset in the cache (architecture v0.2, section 4.7):
/// unloaded -> decoding -> ready -> evicted -> decoding ..., decoding -> failed -> decoding
/// (retry).
enum class AssetState
{
    unloaded,
    decoding,
    ready,
    evicted,
    failed,
};

struct AssetStatus
{
    AssetState state = AssetState::unloaded;
    float progress = 0.0f;             ///< 0...1 while decoding.
    DecodedAudioPtr audio;             ///< Set when ready.
    std::optional<AudioFileInfo> info; ///< Known once the header was parsed.
    std::optional<core::Error> error;  ///< Set when failed.
    /// Waveform peaks: from the peak cache as soon as the file is hashed, otherwise computed
    /// while decoding. Kept when the audio is evicted, so the waveform stays visible.
    PeakDataPtr peaks;
    std::optional<ContentHash> contentHash; ///< Known once hashed (only with a peak cache).
};

/// Decodes assets in the background and keeps them in memory within a budget.
///
/// Thread safety: every public function may be called from any thread except the audio
/// thread. Decoding runs on the cache's own worker threads, so request() never blocks on I/O.
/// Memory of a buffer is released when the last DecodedAudioPtr goes away: the cache drops its
/// reference on a worker or the calling thread, a render graph drops its own when the graph is
/// retired (never on the audio thread, architecture v0.2, section 4.2).
class AudioBufferCache
{
public:
    struct Options
    {
        MemoryBudget budget;
        std::shared_ptr<const ISystemMemory> systemMemory;
        DecoderRegistry decoders;
        int workerThreads = 2;
        /// Global peak cache on disk; null keeps peaks in memory only and skips hashing.
        std::shared_ptr<const PeakCache> peakCache;
    };

    /// Called after every state change. It runs on a worker thread or on the thread that caused
    /// the change, without cache locks held; it must not block for long.
    using Listener = std::function<void(AssetKey, AssetState)>;

    explicit AudioBufferCache(Options options);
    ~AudioBufferCache();

    AudioBufferCache(const AudioBufferCache&) = delete;
    AudioBufferCache& operator=(const AudioBufferCache&) = delete;
    AudioBufferCache(AudioBufferCache&&) = delete;
    AudioBufferCache& operator=(AudioBufferCache&&) = delete;

    /// Set before the first request.
    void setListener(Listener listener);

    /// Starts decoding unless the asset is already ready or decoding. A failed asset is retried.
    void request(AssetKey key, const std::filesystem::path& file);

    /// Adds audio that was produced in memory (generated test signals, Render Preview results)
    /// as a ready asset. Evicts like a decode; fails with outOfMemory above the hard budget.
    core::Result<void> insertDecoded(AssetKey key, DecodedAudioPtr audio);

    /// Stops a running decode; the asset goes back to unloaded.
    void cancel(AssetKey key);

    /// Forgets the asset and drops the cache's reference to its buffer.
    void remove(AssetKey key);

    [[nodiscard]] AssetStatus status(AssetKey key) const;

    /// The decoded buffer if ready (marks it as recently used), otherwise nullptr.
    [[nodiscard]] DecodedAudioPtr acquire(AssetKey key);

    /// Assets referenced by the current document or a live graph; they are never evicted.
    void setPinned(const std::vector<AssetKey>& keys);

    /// Evicts every unpinned ready asset (low system memory). Returns how many were evicted.
    std::size_t evictUnpinned();

    /// Bytes held by ready buffers plus bytes reserved by running decodes.
    [[nodiscard]] std::uint64_t usedBytes() const;

    [[nodiscard]] const MemoryBudget& budget() const noexcept { return options_.budget; }

    /// Blocks until no decode is queued or running, or the timeout passes. For tests and shutdown.
    bool waitUntilIdle(std::chrono::milliseconds timeout);

private:
    struct Job
    {
        AssetKey key;
        std::filesystem::path file;
        std::atomic<bool> cancelled{false};
        std::atomic<float> progress{0.0f};
        std::uint64_t reservedBytes = 0; // guarded by mutex_
    };

    struct Entry
    {
        std::filesystem::path file;
        AssetState state = AssetState::unloaded;
        std::shared_ptr<Job> job; // the decode whose result this entry accepts
        DecodedAudioPtr audio;
        std::optional<AudioFileInfo> info;
        std::optional<core::Error> error;
        PeakDataPtr peaks;
        std::optional<ContentHash> contentHash;
        std::uint64_t lastUse = 0;
    };

    struct Notification
    {
        AssetKey key;
        AssetState state = AssetState::unloaded;
    };

    void workerLoop(const std::stop_token& stop);
    void runJob(const std::shared_ptr<Job>& job);
    std::optional<core::Error> reserveMemory(Job& job, std::uint64_t bytes,
                                             std::vector<Notification>& notes,
                                             std::vector<DecodedAudioPtr>& released);
    void finishJob(const std::shared_ptr<Job>& job, core::Result<DecodedAudioPtr> result);
    void evictForLocked(std::uint64_t incomingBytes, std::vector<Notification>& notes,
                        std::vector<DecodedAudioPtr>& released);
    void notify(const std::vector<Notification>& notes) const;

    Options options_;
    Listener listener_;

    mutable std::mutex mutex_;
    std::condition_variable_any workAvailable_;
    std::condition_variable idle_;
    std::unordered_map<AssetKey, Entry> entries_;
    std::unordered_set<AssetKey> pinned_;
    std::deque<std::shared_ptr<Job>> queue_;
    int running_ = 0;
    std::uint64_t readyBytes_ = 0;
    std::uint64_t reservedBytes_ = 0;
    std::uint64_t useClock_ = 0;

    std::vector<std::jthread> workers_; // last member: joined before the rest is destroyed
};

} // namespace af::assets
