#include "assets/AudioBufferCache.h"

#include <algorithm>
#include <array>
#include <new>
#include <string>

namespace af::assets
{

namespace
{

using core::ErrorCode;
using core::makeError;

constexpr core::FrameCount decodeBlockFrames = 65'536;

std::string pathText(const std::filesystem::path& file)
{
    const auto u8 = file.u8string();
    return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

std::string megabytes(std::uint64_t bytes)
{
    return std::to_string((bytes + mebibyte - 1) / mebibyte) + " MB";
}

} // namespace

DecodedAudio::DecodedAudio(core::SampleRate sampleRate, int channels, core::FrameCount frames)
    : sampleRate_(sampleRate)
    , channels_(channels)
    , frames_(frames)
    , samples_(static_cast<std::size_t>(channels) * static_cast<std::size_t>(frames))
{
}

AudioBufferCache::AudioBufferCache(Options options)
    : options_(std::move(options))
{
    if (!options_.systemMemory)
    {
        options_.systemMemory = ISystemMemory::host();
    }
    const int threads = std::max(1, options_.workerThreads);
    workers_.reserve(static_cast<std::size_t>(threads));
    for (int i = 0; i < threads; ++i)
    {
        workers_.emplace_back([this](const std::stop_token& stop) { workerLoop(stop); });
    }
}

AudioBufferCache::~AudioBufferCache()
{
    {
        const std::scoped_lock lock{mutex_};
        for (auto& job : queue_)
        {
            job->cancelled = true;
        }
        for (auto& [key, entry] : entries_)
        {
            if (entry.job)
            {
                entry.job->cancelled = true;
            }
        }
    }
    for (auto& worker : workers_)
    {
        worker.request_stop();
    }
    workAvailable_.notify_all();
    workers_.clear(); // joins
}

void AudioBufferCache::setListener(Listener listener)
{
    const std::scoped_lock lock{mutex_};
    listener_ = std::move(listener);
}

void AudioBufferCache::request(AssetKey key, const std::filesystem::path& file)
{
    {
        const std::scoped_lock lock{mutex_};
        auto& entry = entries_[key];
        if (entry.state == AssetState::ready || entry.state == AssetState::decoding)
        {
            return;
        }
        auto job = std::make_shared<Job>();
        job->key = key;
        job->file = file;
        entry.file = file;
        entry.state = AssetState::decoding;
        entry.error.reset();
        entry.job = job;
        queue_.push_back(std::move(job));
    }
    workAvailable_.notify_one();
    notify({{key, AssetState::decoding}});
}

void AudioBufferCache::cancel(AssetKey key)
{
    {
        const std::scoped_lock lock{mutex_};
        const auto it = entries_.find(key);
        if (it == entries_.end() || it->second.state != AssetState::decoding)
        {
            return;
        }
        it->second.job->cancelled = true;
        it->second.job.reset();
        it->second.state = AssetState::unloaded;
    }
    notify({{key, AssetState::unloaded}});
}

void AudioBufferCache::remove(AssetKey key)
{
    DecodedAudioPtr released;
    {
        const std::scoped_lock lock{mutex_};
        const auto it = entries_.find(key);
        if (it == entries_.end())
        {
            return;
        }
        if (it->second.job)
        {
            it->second.job->cancelled = true;
        }
        if (it->second.state == AssetState::ready)
        {
            readyBytes_ -= it->second.audio->bytes();
        }
        released = std::move(it->second.audio);
        entries_.erase(it);
    }
    // `released` is destroyed here, outside the lock.
}

AssetStatus AudioBufferCache::status(AssetKey key) const
{
    const std::scoped_lock lock{mutex_};
    const auto it = entries_.find(key);
    if (it == entries_.end())
    {
        return {};
    }
    const auto& entry = it->second;
    AssetStatus status;
    status.state = entry.state;
    status.audio = entry.audio;
    status.info = entry.info;
    status.error = entry.error;
    if (entry.state == AssetState::decoding && entry.job)
    {
        status.progress = entry.job->progress.load();
    }
    else if (entry.state == AssetState::ready)
    {
        status.progress = 1.0f;
    }
    return status;
}

DecodedAudioPtr AudioBufferCache::acquire(AssetKey key)
{
    const std::scoped_lock lock{mutex_};
    const auto it = entries_.find(key);
    if (it == entries_.end() || it->second.state != AssetState::ready)
    {
        return nullptr;
    }
    it->second.lastUse = ++useClock_;
    return it->second.audio;
}

void AudioBufferCache::setPinned(const std::vector<AssetKey>& keys)
{
    std::vector<Notification> notes;
    std::vector<DecodedAudioPtr> released;
    {
        const std::scoped_lock lock{mutex_};
        pinned_ = {keys.begin(), keys.end()};
        evictForLocked(0, notes, released);
    }
    released.clear();
    notify(notes);
}

std::size_t AudioBufferCache::evictUnpinned()
{
    std::vector<Notification> notes;
    std::vector<DecodedAudioPtr> released;
    {
        const std::scoped_lock lock{mutex_};
        for (auto& [key, entry] : entries_)
        {
            if (entry.state == AssetState::ready && !pinned_.contains(key))
            {
                readyBytes_ -= entry.audio->bytes();
                released.push_back(std::move(entry.audio));
                entry.state = AssetState::evicted;
                notes.push_back({key, AssetState::evicted});
            }
        }
    }
    released.clear();
    notify(notes);
    return notes.size();
}

std::uint64_t AudioBufferCache::usedBytes() const
{
    const std::scoped_lock lock{mutex_};
    return readyBytes_ + reservedBytes_;
}

bool AudioBufferCache::waitUntilIdle(std::chrono::milliseconds timeout)
{
    std::unique_lock lock{mutex_};
    return idle_.wait_for(lock, timeout, [this] { return queue_.empty() && running_ == 0; });
}

void AudioBufferCache::workerLoop(const std::stop_token& stop)
{
    while (true)
    {
        std::shared_ptr<Job> job;
        {
            std::unique_lock lock{mutex_};
            if (!workAvailable_.wait(lock, stop, [this] { return !queue_.empty(); }))
            {
                return; // stop requested
            }
            job = std::move(queue_.front());
            queue_.pop_front();
            ++running_;
        }

        runJob(job);

        {
            const std::scoped_lock lock{mutex_};
            --running_;
        }
        idle_.notify_all();
    }
}

void AudioBufferCache::runJob(const std::shared_ptr<Job>& job)
{
    if (job->cancelled)
    {
        finishJob(job, makeError(ErrorCode::cancelled, "Decoding was cancelled"));
        return;
    }

    auto opened = options_.decoders.open(job->file);
    if (!opened)
    {
        finishJob(job, opened.error());
        return;
    }
    auto& reader = *opened.value();
    const AudioFileInfo info = reader.info();

    {
        const std::scoped_lock lock{mutex_};
        const auto it = entries_.find(job->key);
        if (it != entries_.end() && it->second.job == job)
        {
            it->second.info = info;
        }
    }

    const auto bytes = info.decodedBytes();
    std::vector<Notification> notes;
    std::vector<DecodedAudioPtr> released;
    auto refused = reserveMemory(*job, bytes, notes, released);
    released.clear();
    notify(notes);
    if (refused)
    {
        finishJob(job, std::move(*refused));
        return;
    }

    try
    {
        auto audio = std::make_shared<DecodedAudio>(info.sampleRate, info.channels, info.frames);
        std::array<float*, maxChannels> pointers{};
        core::FrameCount done = 0;
        while (done < info.frames)
        {
            if (job->cancelled)
            {
                finishJob(job, makeError(ErrorCode::cancelled, "Decoding was cancelled"));
                return;
            }
            for (int ch = 0; ch < info.channels; ++ch)
            {
                pointers[static_cast<std::size_t>(ch)] = audio->writableChannel(ch) + done;
            }
            const auto want = std::min(decodeBlockFrames, info.frames - done);
            auto got = reader.read(
                std::span<float* const>(pointers.data(), static_cast<std::size_t>(info.channels)),
                want);
            if (!got)
            {
                auto error = got.error();
                error.subject = pathText(job->file);
                finishJob(job, std::move(error));
                return;
            }
            if (got.value() == 0)
            {
                finishJob(job,
                          makeError(ErrorCode::invalidData, "File ended before its declared length",
                                    pathText(job->file)));
                return;
            }
            done += got.value();
            job->progress =
                static_cast<float>(static_cast<double>(done) / static_cast<double>(info.frames));
        }
        finishJob(job, DecodedAudioPtr{std::move(audio)});
    }
    catch (const std::bad_alloc&)
    {
        finishJob(job, makeError(ErrorCode::outOfMemory,
                                 "Not enough memory to decode " + megabytes(bytes),
                                 pathText(job->file)));
    }
}

std::optional<core::Error> AudioBufferCache::reserveMemory(Job& job, std::uint64_t bytes,
                                                           std::vector<Notification>& notes,
                                                           std::vector<DecodedAudioPtr>& released)
{
    const auto subject = pathText(job.file);
    const auto& budget = options_.budget;
    if (bytes > budget.maxAssetBytes)
    {
        return makeError(ErrorCode::outOfMemory,
                         "Asset needs " + megabytes(bytes) + " after decoding; the limit is " +
                             megabytes(budget.maxAssetBytes),
                         subject);
    }

    const std::scoped_lock lock{mutex_};
    if (job.cancelled)
    {
        return makeError(ErrorCode::cancelled, "Decoding was cancelled");
    }
    evictForLocked(bytes, notes, released);
    const auto used = readyBytes_ + reservedBytes_;
    if (used + bytes > budget.hardBytes)
    {
        return makeError(ErrorCode::outOfMemory,
                         "Memory budget exceeded: assets use " + megabytes(used) +
                             ", this one needs " + megabytes(bytes) + ", the limit is " +
                             megabytes(budget.hardBytes),
                         subject);
    }
    const auto available = options_.systemMemory->availableBytes();
    if (available < bytes || available - bytes < budget.systemReserveBytes)
    {
        return makeError(ErrorCode::outOfMemory,
                         "Not enough free system memory: " + megabytes(available) + " available, " +
                             megabytes(bytes) + " needed plus " +
                             megabytes(budget.systemReserveBytes) + " kept for the system",
                         subject);
    }
    job.reservedBytes = bytes;
    reservedBytes_ += bytes;
    return std::nullopt;
}

void AudioBufferCache::evictForLocked(std::uint64_t incomingBytes, std::vector<Notification>& notes,
                                      std::vector<DecodedAudioPtr>& released)
{
    while (readyBytes_ + reservedBytes_ + incomingBytes > options_.budget.softBytes)
    {
        Entry* oldest = nullptr;
        AssetKey oldestKey;
        for (auto& [key, entry] : entries_)
        {
            if (entry.state == AssetState::ready && !pinned_.contains(key) &&
                (oldest == nullptr || entry.lastUse < oldest->lastUse))
            {
                oldest = &entry;
                oldestKey = key;
            }
        }
        if (oldest == nullptr)
        {
            return; // Only pinned assets left; the hard budget decides.
        }
        readyBytes_ -= oldest->audio->bytes();
        released.push_back(std::move(oldest->audio));
        oldest->state = AssetState::evicted;
        notes.push_back({oldestKey, AssetState::evicted});
    }
}

void AudioBufferCache::finishJob(const std::shared_ptr<Job>& job,
                                 core::Result<DecodedAudioPtr> result)
{
    std::optional<Notification> note;
    {
        const std::scoped_lock lock{mutex_};
        reservedBytes_ -= job->reservedBytes;
        job->reservedBytes = 0;

        const auto it = entries_.find(job->key);
        if (it == entries_.end() || it->second.job != job)
        {
            return; // Removed or cancelled meanwhile: the result is discarded.
        }
        auto& entry = it->second;
        entry.job.reset();
        if (result)
        {
            entry.audio = std::move(result).value();
            entry.state = AssetState::ready;
            entry.lastUse = ++useClock_;
            readyBytes_ += entry.audio->bytes();
        }
        else if (result.error().code == ErrorCode::cancelled)
        {
            entry.state = AssetState::unloaded;
        }
        else
        {
            entry.error = result.error();
            entry.state = AssetState::failed;
        }
        note = Notification{job->key, entry.state};
    }
    notify({*note});
}

void AudioBufferCache::notify(const std::vector<Notification>& notes) const
{
    if (notes.empty())
    {
        return;
    }
    Listener listener;
    {
        const std::scoped_lock lock{mutex_};
        listener = listener_;
    }
    if (listener)
    {
        for (const auto& note : notes)
        {
            listener(note.key, note.state);
        }
    }
}

} // namespace af::assets
