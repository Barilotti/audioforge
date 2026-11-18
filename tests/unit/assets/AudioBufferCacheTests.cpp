#include "TestFiles.h"
#include "assets/AudioBufferCache.h"
#include "assets/AudioDecoder.h"
#include "assets/ContentHash.h"
#include "assets/PeakCache.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace af::assets;
using namespace std::chrono_literals;
using af::core::ErrorCode;
using af::core::FrameCount;
using af::core::Uuid;
using af::test::fixtureFrames;
using af::test::fixtureSample;
using af::test::TempDir;
using af::test::wavFixture;
using af::test::writeBytes;

namespace
{

constexpr std::uint64_t fixtureBytes =
    std::uint64_t{301} * 2 * sizeof(float); // decoded s16 stereo fixture
const auto stereoFixture = wavFixture("pcm_s16_stereo_48000.wav");

class FakeMemory final : public ISystemMemory
{
public:
    explicit FakeMemory(std::uint64_t available)
        : available_(available)
    {
    }

    [[nodiscard]] std::uint64_t physicalBytes() const override { return 16 * gibibyte; }
    [[nodiscard]] std::uint64_t availableBytes() const override { return available_.load(); }
    void setAvailable(std::uint64_t bytes) { available_ = bytes; }

private:
    std::atomic<std::uint64_t> available_;
};

/// A decoder for files starting with "FAKE" whose reader blocks until the test opens the gate,
/// so tests can observe the decoding state and cancel it.
struct Gate
{
    std::atomic<bool> open{false};
    std::atomic<int> readsStarted{0};
    FrameCount frames = 1'000'000;
};

class GatedDecoder final : public IAudioDecoder
{
public:
    explicit GatedDecoder(std::shared_ptr<Gate> gate)
        : gate_(std::move(gate))
    {
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "fake"; }

    [[nodiscard]] bool recognises(std::span<const unsigned char> header) const noexcept override
    {
        return header.size() >= 4 && std::memcmp(header.data(), "FAKE", 4) == 0;
    }

    [[nodiscard]] af::core::Result<std::unique_ptr<IAudioReader>>
    open(const std::filesystem::path&) const override
    {
        return std::unique_ptr<IAudioReader>(std::make_unique<Reader>(gate_));
    }

private:
    class Reader final : public IAudioReader
    {
    public:
        explicit Reader(std::shared_ptr<Gate> gate)
            : gate_(std::move(gate))
        {
            info_.sampleRate = 48'000;
            info_.channels = 1;
            info_.frames = gate_->frames;
        }

        [[nodiscard]] const AudioFileInfo& info() const noexcept override { return info_; }

        af::core::Result<FrameCount> read(std::span<float* const> channels,
                                          FrameCount maxFrames) override
        {
            ++gate_->readsStarted;
            while (!gate_->open)
            {
                std::this_thread::sleep_for(1ms);
            }
            const auto n = std::min(maxFrames, info_.frames - position_);
            std::fill_n(channels[0], n, 0.25f);
            position_ += n;
            return n;
        }

    private:
        std::shared_ptr<Gate> gate_;
        AudioFileInfo info_;
        FrameCount position_ = 0;
    };

    std::shared_ptr<Gate> gate_;
};

struct Recorder
{
    std::mutex mutex;
    std::vector<std::pair<Uuid, AssetState>> events;

    AudioBufferCache::Listener listener()
    {
        return [this](Uuid key, AssetState state)
        {
            const std::scoped_lock lock{mutex};
            events.emplace_back(key, state);
        };
    }

    std::vector<AssetState> statesOf(Uuid key)
    {
        const std::scoped_lock lock{mutex};
        std::vector<AssetState> states;
        for (const auto& [k, s] : events)
        {
            if (k == key)
            {
                states.push_back(s);
            }
        }
        return states;
    }
};

AudioBufferCache::Options options(std::uint64_t soft, std::uint64_t hard,
                                  std::shared_ptr<FakeMemory> memory = nullptr,
                                  std::shared_ptr<Gate> gate = nullptr)
{
    AudioBufferCache::Options o;
    o.budget.softBytes = soft;
    o.budget.hardBytes = hard;
    o.budget.systemReserveBytes = 0;
    o.systemMemory = memory ? std::move(memory) : std::make_shared<FakeMemory>(64 * gibibyte);
    o.decoders = DecoderRegistry::withBuiltInDecoders();
    if (gate)
    {
        o.decoders.add(std::make_shared<GatedDecoder>(std::move(gate)));
    }
    return o;
}

AssetState decodeAndWait(AudioBufferCache& cache, Uuid key, const std::filesystem::path& file)
{
    cache.request(key, file);
    REQUIRE(cache.waitUntilIdle(10s));
    return cache.status(key).state;
}

template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = 10s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

} // namespace

TEST_CASE("Default budgets follow ADR-0006", "[assets][cache]")
{
    const auto small = MemoryBudget::defaultsFor(8 * gibibyte);
    REQUIRE(small.hardBytes == 4 * gibibyte);
    REQUIRE(small.softBytes == 8 * gibibyte / 5 * 2); // 40% of RAM
    REQUIRE(small.systemReserveBytes == gibibyte);
    REQUIRE(small.maxAssetBytes == gibibyte);
    REQUIRE(small.largeAssetBytes == 256 * mebibyte);

    const auto large = MemoryBudget::defaultsFor(64 * gibibyte);
    REQUIRE(large.hardBytes == 8 * gibibyte);
    REQUIRE(large.softBytes == 8 * gibibyte / 5 * 4); // 80% of hard, not 40% of RAM
    REQUIRE(large.systemReserveBytes == 64 * gibibyte / 10);

    REQUIRE(ISystemMemory::host()->physicalBytes() > 0);
    REQUIRE(ISystemMemory::host()->availableBytes() > 0);
}

TEST_CASE("A requested asset is decoded in the background", "[assets][cache]")
{
    AudioBufferCache cache{options(gibibyte, gibibyte)};
    Recorder recorder;
    cache.setListener(recorder.listener());
    const auto key = Uuid::generate();

    REQUIRE(cache.status(key).state == AssetState::unloaded);
    REQUIRE(cache.acquire(key) == nullptr);
    REQUIRE(decodeAndWait(cache, key, stereoFixture) == AssetState::ready);

    const auto status = cache.status(key);
    REQUIRE(status.progress == 1.0f);
    REQUIRE(status.info.has_value());
    REQUIRE(status.info->frames == fixtureFrames);
    const auto audio = cache.acquire(key);
    REQUIRE(audio != nullptr);
    REQUIRE(audio->sampleRate() == 48'000);
    REQUIRE(audio->channels() == 2);
    REQUIRE(audio->frames() == fixtureFrames);
    for (FrameCount i = 0; i < fixtureFrames; ++i)
    {
        REQUIRE(audio->channel(0)[i] == fixtureSample(i, 0));
        REQUIRE(audio->channel(1)[i] == fixtureSample(i, 1));
    }
    REQUIRE(cache.usedBytes() == fixtureBytes);
    REQUIRE(recorder.statesOf(key) == std::vector{AssetState::decoding, AssetState::ready});

    // A second request for a ready asset changes nothing.
    cache.request(key, stereoFixture);
    REQUIRE(cache.acquire(key) == audio);
}

TEST_CASE("Failed decodes keep the reason and can be retried", "[assets][cache]")
{
    AudioBufferCache cache{options(gibibyte, gibibyte)};
    const TempDir dir;
    const auto key = Uuid::generate();
    const auto file = dir / "later.wav";

    REQUIRE(decodeAndWait(cache, key, file) == AssetState::failed);
    REQUIRE(cache.status(key).error->code == ErrorCode::notFound);
    REQUIRE(cache.usedBytes() == 0);

    std::filesystem::copy_file(stereoFixture, file);
    REQUIRE(decodeAndWait(cache, key, file) == AssetState::ready);
    REQUIRE_FALSE(cache.status(key).error.has_value());

    const auto broken = Uuid::generate();
    af::test::writeBytes(dir / "broken.wav", {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'});
    REQUIRE(decodeAndWait(cache, broken, dir / "broken.wav") == AssetState::failed);
    REQUIRE(cache.status(broken).error->code == ErrorCode::invalidData);
}

TEST_CASE("Assets above the single-asset limit are refused", "[assets][cache]")
{
    auto o = options(gibibyte, gibibyte);
    o.budget.maxAssetBytes = fixtureBytes - 1;
    AudioBufferCache cache{std::move(o)};
    const auto key = Uuid::generate();
    REQUIRE(decodeAndWait(cache, key, stereoFixture) == AssetState::failed);
    REQUIRE(cache.status(key).error->code == ErrorCode::outOfMemory);
    REQUIRE(cache.status(key).info.has_value()); // the header was read, so the UI can show the size
}

TEST_CASE("Unpinned assets are evicted least recently used first above the soft budget",
          "[assets][cache]")
{
    AudioBufferCache cache{options(2 * fixtureBytes, 100 * fixtureBytes)};
    Recorder recorder;
    cache.setListener(recorder.listener());
    const auto a = Uuid::generate();
    const auto b = Uuid::generate();
    const auto c = Uuid::generate();

    REQUIRE(decodeAndWait(cache, a, stereoFixture) == AssetState::ready);
    REQUIRE(decodeAndWait(cache, b, stereoFixture) == AssetState::ready);
    const auto keptByGraph = cache.acquire(a); // a is now more recently used than b

    REQUIRE(decodeAndWait(cache, c, stereoFixture) == AssetState::ready);
    REQUIRE(cache.status(a).state == AssetState::ready);
    REQUIRE(cache.status(b).state == AssetState::evicted);
    REQUIRE(cache.acquire(b) == nullptr);
    REQUIRE(cache.usedBytes() == 2 * fixtureBytes);
    REQUIRE(recorder.statesOf(b).back() == AssetState::evicted);

    // An evicted asset decodes again on request.
    REQUIRE(decodeAndWait(cache, b, stereoFixture) == AssetState::ready);
    REQUIRE(cache.status(a).state == AssetState::evicted); // now a was the oldest
    REQUIRE(keptByGraph->frames() == fixtureFrames);       // a holder keeps its buffer alive
}

TEST_CASE("Pinned assets are never evicted; the hard budget refuses new decodes", "[assets][cache]")
{
    AudioBufferCache cache{options(fixtureBytes, 2 * fixtureBytes)};
    const auto a = Uuid::generate();
    const auto b = Uuid::generate();
    const auto c = Uuid::generate();

    cache.setPinned({a, b});
    REQUIRE(decodeAndWait(cache, a, stereoFixture) == AssetState::ready);
    REQUIRE(decodeAndWait(cache, b, stereoFixture) == AssetState::ready);
    REQUIRE(cache.usedBytes() == 2 * fixtureBytes); // above soft, but everything is pinned

    REQUIRE(decodeAndWait(cache, c, stereoFixture) == AssetState::failed);
    REQUIRE(cache.status(c).error->code == ErrorCode::outOfMemory);
    REQUIRE(cache.status(a).state == AssetState::ready);
    REQUIRE(cache.status(b).state == AssetState::ready);

    // Unpinning b lets the cache evict it down to the soft budget.
    cache.setPinned({a});
    REQUIRE(cache.status(b).state == AssetState::evicted);
    REQUIRE(cache.usedBytes() == fixtureBytes);
    // a (pinned) plus c exceed the soft budget but fit the hard one.
    REQUIRE(decodeAndWait(cache, c, stereoFixture) == AssetState::ready);
    REQUIRE(cache.status(a).state == AssetState::ready);
}

TEST_CASE("Decoding is refused when the system would run short of memory", "[assets][cache]")
{
    auto memory = std::make_shared<FakeMemory>(fixtureBytes + 1000);
    auto o = options(gibibyte, gibibyte, memory);
    o.budget.systemReserveBytes = 2000;
    AudioBufferCache cache{std::move(o)};
    const auto key = Uuid::generate();

    REQUIRE(decodeAndWait(cache, key, stereoFixture) == AssetState::failed);
    REQUIRE(cache.status(key).error->code == ErrorCode::outOfMemory);

    memory->setAvailable(fixtureBytes + 2000);
    REQUIRE(decodeAndWait(cache, key, stereoFixture) == AssetState::ready);
}

TEST_CASE("Low-memory eviction drops every unpinned asset", "[assets][cache]")
{
    AudioBufferCache cache{options(gibibyte, gibibyte)};
    const auto a = Uuid::generate();
    const auto b = Uuid::generate();
    cache.setPinned({a});
    REQUIRE(decodeAndWait(cache, a, stereoFixture) == AssetState::ready);
    REQUIRE(decodeAndWait(cache, b, stereoFixture) == AssetState::ready);
    REQUIRE(cache.evictUnpinned() == 1);
    REQUIRE(cache.status(a).state == AssetState::ready);
    REQUIRE(cache.status(b).state == AssetState::evicted);
    REQUIRE(cache.usedBytes() == fixtureBytes);
}

TEST_CASE("request() returns at once and a running decode can be cancelled", "[assets][cache]")
{
    const TempDir dir;
    af::test::writeBytes(dir / "slow.fake", {'F', 'A', 'K', 'E'});
    auto gate = std::make_shared<Gate>();
    AudioBufferCache cache{options(gibibyte, gibibyte, nullptr, gate)};
    Recorder recorder;
    cache.setListener(recorder.listener());
    const auto key = Uuid::generate();

    const auto start = std::chrono::steady_clock::now();
    cache.request(key, dir / "slow.fake");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < 100ms); // the caller (UI thread) never waits for the decode

    REQUIRE(waitFor([&] { return gate->readsStarted.load() > 0; }));
    REQUIRE(cache.status(key).state == AssetState::decoding);
    REQUIRE(cache.usedBytes() ==
            static_cast<std::uint64_t>(gate->frames) * sizeof(float)); // reserved

    cache.cancel(key);
    REQUIRE(cache.status(key).state == AssetState::unloaded);
    gate->open = true;
    REQUIRE(cache.waitUntilIdle(10s));
    REQUIRE(cache.status(key).state == AssetState::unloaded); // the late result is discarded
    REQUIRE(cache.usedBytes() == 0);
    REQUIRE(recorder.statesOf(key) == std::vector{AssetState::decoding, AssetState::unloaded});

    // After cancelling, a new request decodes normally and reports progress up to 1.
    REQUIRE(decodeAndWait(cache, key, dir / "slow.fake") == AssetState::ready);
    REQUIRE(cache.acquire(key)->channel(0)[gate->frames - 1] == 0.25f);
}

TEST_CASE("Removing a decoding asset discards its result; the cache can be destroyed mid-decode",
          "[assets][cache]")
{
    const TempDir dir;
    af::test::writeBytes(dir / "slow.fake", {'F', 'A', 'K', 'E'});
    auto gate = std::make_shared<Gate>();
    const auto key = Uuid::generate();
    {
        AudioBufferCache cache{options(gibibyte, gibibyte, nullptr, gate)};
        cache.request(key, dir / "slow.fake");
        REQUIRE(waitFor([&] { return gate->readsStarted.load() > 0; }));
        cache.remove(key);
        REQUIRE(cache.status(key).state == AssetState::unloaded);

        cache.request(Uuid::generate(), dir / "slow.fake"); // still decoding when the cache dies
        std::thread opener{[gate]
                           {
                               std::this_thread::sleep_for(50ms);
                               gate->open = true;
                           }};
        opener.detach();
    }
    SUCCEED("cache destroyed while a decode was running");
}

TEST_CASE("Many assets decode concurrently without losing any", "[assets][cache][stress]")
{
    AudioBufferCache cache{options(gibibyte, gibibyte)};
    std::vector<Uuid> keys(64);
    for (auto& key : keys)
    {
        key = Uuid::generate();
        cache.request(key, stereoFixture);
    }
    REQUIRE(cache.waitUntilIdle(30s));
    for (const auto& key : keys)
    {
        REQUIRE(cache.status(key).state == AssetState::ready);
    }
    REQUIRE(cache.usedBytes() == keys.size() * fixtureBytes);
}

#if defined(AF_TEST_HUGE_ALLOCATIONS)
TEST_CASE("An allocation failure while decoding fails the asset, not the application",
          "[assets][cache]")
{
    const TempDir dir;
    af::test::writeBytes(dir / "huge.fake", {'F', 'A', 'K', 'E'});
    auto gate = std::make_shared<Gate>();
    gate->frames = FrameCount{1} << 42; // 16 TB of float32
    gate->open = true;
    auto o = options(~std::uint64_t{0}, ~std::uint64_t{0},
                     std::make_shared<FakeMemory>(~std::uint64_t{0}), gate);
    o.budget.maxAssetBytes = ~std::uint64_t{0};
    AudioBufferCache cache{std::move(o)};
    const auto key = Uuid::generate();
    REQUIRE(decodeAndWait(cache, key, dir / "huge.fake") == AssetState::failed);
    REQUIRE(cache.status(key).error->code == ErrorCode::outOfMemory);
    REQUIRE(cache.usedBytes() == 0);
}
#endif

TEST_CASE("Audio produced in memory can be inserted as a ready asset", "[assets][cache]")
{
    AudioBufferCache cache{options(gibibyte, 1000)};
    const auto key = Uuid::generate();
    auto audio = std::make_shared<DecodedAudio>(48'000, 1, 100);
    REQUIRE(cache.insertDecoded(key, audio).ok());
    REQUIRE(cache.status(key).state == AssetState::ready);
    REQUIRE(cache.acquire(key) == audio);
    REQUIRE(cache.usedBytes() == 400);

    const auto tooBig =
        cache.insertDecoded(Uuid::generate(), std::make_shared<DecodedAudio>(48'000, 1, 200));
    REQUIRE_FALSE(tooBig.ok());
    REQUIRE(tooBig.error().code == ErrorCode::outOfMemory);
    REQUIRE_FALSE(cache.insertDecoded(key, nullptr).ok());
}

TEST_CASE("Decoding computes peaks in the same pass and stores them in the peak cache",
          "[assets][cache][peaks]")
{
    const TempDir dir;
    auto o = options(gibibyte, gibibyte);
    o.peakCache = std::make_shared<PeakCache>(dir / "peaks");
    AudioBufferCache cache{std::move(o)};

    const auto key = Uuid::generate();
    REQUIRE(decodeAndWait(cache, key, stereoFixture) == AssetState::ready);
    const auto status = cache.status(key);
    REQUIRE(status.peaks != nullptr);
    REQUIRE(status.contentHash.has_value());
    REQUIRE(*status.contentHash == hashFile(stereoFixture).value());

    // The reference: peaks built from the decoded samples.
    PeakBuilder reference{status.audio->sampleRate(), 2, status.audio->frames()};
    std::array<const float*, 2> channels{status.audio->channel(0), status.audio->channel(1)};
    reference.add(channels, status.audio->frames());
    const auto expected = reference.finish();
    REQUIRE(std::equal(status.peaks->storage().begin(), status.peaks->storage().end(),
                       expected->storage().begin(), expected->storage().end()));
    REQUIRE(status.peaks->level(0, 0)[0].max > 0.5f);

    REQUIRE(std::filesystem::exists(dir / "peaks" / (status.contentHash->toHex() + ".afpk")));

    // Eviction drops the audio but keeps the peaks for the waveform.
    REQUIRE(cache.evictUnpinned() == 1);
    REQUIRE(cache.status(key).state == AssetState::evicted);
    REQUIRE(cache.status(key).peaks == status.peaks);
}

TEST_CASE("Opening known content takes the peaks from the cache before decoding finishes",
          "[assets][cache][peaks]")
{
    const TempDir dir;
    const std::vector<unsigned char> fakeFile{'F', 'A', 'K', 'E', 1, 2, 3};
    writeBytes(dir / "first name.fake", fakeFile);
    writeBytes(dir / "moved and renamed.fake", fakeFile);

    // Peaks a decode would never produce (the fake signal is a constant 0.25), so a match
    // proves they came from the cache file.
    auto gate = std::make_shared<Gate>();
    gate->frames = 10'000;
    const auto peakCache = std::make_shared<PeakCache>(dir / "peaks");
    PeakData planted{48'000, 1, gate->frames};
    for (auto& peak : planted.writableStorage())
    {
        peak = {-0.75f, 0.5f};
    }
    REQUIRE(peakCache->store(hashFile(dir / "first name.fake").value(), planted));

    auto o = options(gibibyte, gibibyte, nullptr, gate);
    o.peakCache = peakCache;
    AudioBufferCache cache{std::move(o)};
    const auto key = Uuid::generate();
    cache.request(key, dir / "moved and renamed.fake");

    REQUIRE(waitFor([&] { return cache.status(key).peaks != nullptr; }));
    const auto early = cache.status(key);
    REQUIRE(early.state == AssetState::decoding); // the gate is still closed
    REQUIRE(early.peaks->range(0, 0, 10'000) == MinMax{-0.75f, 0.5f});

    gate->open = true;
    REQUIRE(cache.waitUntilIdle(10s));
    const auto done = cache.status(key);
    REQUIRE(done.state == AssetState::ready);
    REQUIRE(done.peaks == early.peaks); // not recomputed
}

TEST_CASE("Cached peaks that do not fit the file are ignored and rebuilt", "[assets][cache][peaks]")
{
    const TempDir dir;
    const auto peakCache = std::make_shared<PeakCache>(dir / "peaks");
    const PeakData wrongLength{48'000, 2, 5};
    REQUIRE(peakCache->store(hashFile(stereoFixture).value(), wrongLength));

    auto o = options(gibibyte, gibibyte);
    o.peakCache = peakCache;
    AudioBufferCache cache{std::move(o)};
    const auto key = Uuid::generate();
    REQUIRE(decodeAndWait(cache, key, stereoFixture) == AssetState::ready);
    REQUIRE(cache.status(key).peaks->frames() == fixtureFrames);
    REQUIRE(peakCache->load(hashFile(stereoFixture).value())->frames() == fixtureFrames);
}

TEST_CASE("Audio inserted from memory gets peaks too", "[assets][cache][peaks]")
{
    AudioBufferCache cache{options(gibibyte, gibibyte)};
    auto audio = std::make_shared<DecodedAudio>(48'000, 1, 1000);
    audio->writableChannel(0)[300] = -0.5f;
    const auto key = Uuid::generate();
    REQUIRE(cache.insertDecoded(key, audio));
    const auto peaks = cache.status(key).peaks;
    REQUIRE(peaks != nullptr);
    REQUIRE(peaks->level(0, 0)[1] == MinMax{-0.5f, 0.0f});
    REQUIRE(peaks->range(0, 0, 1000) == MinMax{-0.5f, 0.0f});
}
