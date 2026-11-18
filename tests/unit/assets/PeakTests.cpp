#include "TestFiles.h"
#include "assets/ContentHash.h"
#include "assets/PeakCache.h"
#include "assets/PeakData.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <atomic>
#include <random>
#include <vector>

using namespace af::assets;
using af::core::FrameCount;
using af::test::readBytes;
using af::test::TempDir;
using af::test::writeBytes;

namespace
{

struct Signal
{
    int channels;
    FrameCount frames;
    std::vector<std::vector<float>> samples;

    Signal(int channelCount, FrameCount frameCount, unsigned seed)
        : channels(channelCount)
        , frames(frameCount)
    {
        std::mt19937 rng{seed};
        std::uniform_real_distribution<float> dist{-1.5f, 1.5f};
        samples.resize(static_cast<std::size_t>(channels));
        for (auto& channel : samples)
        {
            channel.resize(static_cast<std::size_t>(frames));
            std::generate(channel.begin(), channel.end(), [&] { return dist(rng); });
        }
    }

    /// Brute-force min/max of frames [begin, end).
    [[nodiscard]] MinMax exact(int channel, FrameCount begin, FrameCount end) const
    {
        const auto& s = samples[static_cast<std::size_t>(channel)];
        const auto [lo, hi] = std::minmax_element(s.begin() + begin, s.begin() + end);
        return {*lo, *hi};
    }

    /// Feeds a builder in chunks of random size, as a decoder would.
    [[nodiscard]] std::shared_ptr<PeakData> build(unsigned seed) const
    {
        PeakBuilder builder{48'000, channels, frames};
        std::mt19937 rng{seed};
        std::uniform_int_distribution<FrameCount> chunk{1, 9000};
        FrameCount done = 0;
        while (done < frames)
        {
            const auto n = std::min(chunk(rng), frames - done);
            std::vector<const float*> pointers;
            pointers.reserve(samples.size());
            for (const auto& channel : samples)
            {
                pointers.push_back(channel.data() + done);
            }
            builder.add(pointers, n);
            done += n;
        }
        return builder.finish();
    }
};

} // namespace

TEST_CASE("Peaks match a brute-force reference at every level", "[assets][peaks]")
{
    const auto frames = GENERATE(FrameCount{1}, FrameCount{255}, FrameCount{256},
                                 FrameCount{70'001}, FrameCount{200'000});
    const Signal signal{3, frames, 42};
    const auto peaks = signal.build(7);
    REQUIRE(peaks->frames() == frames);
    REQUIRE(peaks->channels() == 3);

    for (int level = 0; level < PeakData::levelCount; ++level)
    {
        const auto block = PeakData::blockFrames[static_cast<std::size_t>(level)];
        REQUIRE(peaks->blockCount(level) == static_cast<std::size_t>((frames + block - 1) / block));
        for (int ch = 0; ch < 3; ++ch)
        {
            const auto values = peaks->level(level, ch);
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                const auto begin = static_cast<FrameCount>(i) * block;
                const auto end = std::min(begin + block, frames);
                REQUIRE(values[i] == signal.exact(ch, begin, end));
            }
        }
    }
}

TEST_CASE("Peak ranges cover the exact range and are exact on block boundaries", "[assets][peaks]")
{
    const Signal signal{2, 300'000, 3};
    const auto peaks = signal.build(11);

    std::mt19937 rng{5};
    std::uniform_int_distribution<FrameCount> position{0, 300'000};
    for (int i = 0; i < 2000; ++i)
    {
        auto a = position(rng);
        auto b = position(rng);
        if (a > b)
        {
            std::swap(a, b);
        }
        if (b - a < 1)
        {
            continue;
        }
        const auto exact = signal.exact(1, a, b);
        const auto approx = peaks->range(1, a, b);
        REQUIRE(approx.min <= exact.min);
        REQUIRE(approx.max >= exact.max);
    }

    for (const auto& [a, b] : {std::pair<FrameCount, FrameCount>{4096, 8192},
                               {0, 65'536},
                               {256, 512},
                               {65'536, 300'000}})
    {
        REQUIRE(peaks->range(0, a, b) == signal.exact(0, a, b));
    }
    REQUIRE(peaks->range(0, -100, 1'000'000) == signal.exact(0, 0, 300'000)); // clamped
    REQUIRE(peaks->range(0, 500, 500) == MinMax{});
    REQUIRE(peaks->range(5, 0, 100) == MinMax{});
}

TEST_CASE("Content hashes depend on the bytes only", "[assets][hash]")
{
    const TempDir dir;
    const std::vector<unsigned char> bytes(3'000'000, 7); // more than one read chunk
    writeBytes(dir / "a.wav", bytes);
    writeBytes(dir / "renamed copy.wav", bytes);
    auto changed = bytes;
    changed[2'999'999] = 8;
    writeBytes(dir / "changed.wav", changed);

    const auto a = hashFile(dir / "a.wav");
    const auto b = hashFile(dir / "renamed copy.wav");
    const auto c = hashFile(dir / "changed.wav");
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    REQUIRE(a.value() == b.value());
    REQUIRE_FALSE(a.value() == c.value());

    const auto hex = a.value().toHex();
    REQUIRE(hex.size() == 32);
    REQUIRE(ContentHash::fromHex(hex) == a.value());
    REQUIRE_FALSE(ContentHash::fromHex("xyz").has_value());

    REQUIRE(hashFile(dir / "missing.wav").error().code == af::core::ErrorCode::ioError);
    const std::atomic<bool> cancel{true};
    REQUIRE(hashFile(dir / "a.wav", &cancel).error().code == af::core::ErrorCode::cancelled);
}

TEST_CASE("The peak cache stores, loads and rejects damaged files", "[assets][peaks]")
{
    const TempDir dir;
    const PeakCache cache{dir / "peaks"};
    const ContentHash key{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
    REQUIRE(cache.load(key) == nullptr);

    const Signal signal{2, 100'000, 9};
    const auto peaks = signal.build(1);
    REQUIRE(cache.store(key, *peaks));
    REQUIRE(cache.fileFor(key).filename() == "0123456789abcdeffedcba9876543210.afpk");

    const auto loaded = cache.load(key);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->sampleRate() == 48'000);
    REQUIRE(loaded->channels() == 2);
    REQUIRE(loaded->frames() == 100'000);
    REQUIRE(std::equal(loaded->storage().begin(), loaded->storage().end(), peaks->storage().begin(),
                       peaks->storage().end()));

    const auto file = cache.fileFor(key);
    const auto good = readBytes(file);
    SECTION("One flipped byte")
    {
        auto bad = good;
        bad[100] ^= 0x40U;
        writeBytes(file, bad);
        REQUIRE(cache.load(key) == nullptr);
    }
    SECTION("Cut short")
    {
        writeBytes(file,
                   {good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() / 2)});
        REQUIRE(cache.load(key) == nullptr);
    }
    SECTION("Another format version")
    {
        auto other = good;
        other[4] = static_cast<unsigned char>(PeakCache::formatVersion + 1);
        writeBytes(file, other);
        REQUIRE(cache.load(key) == nullptr);
    }
    SECTION("Rewritten after damage")
    {
        writeBytes(file, {'A', 'F', 'P', 'K'});
        REQUIRE(cache.load(key) == nullptr);
        REQUIRE(cache.store(key, *peaks));
        REQUIRE(cache.load(key) != nullptr);
    }
}
