#include "TestFiles.h"
#include "assets/AudioDecoder.h"
#include "assets/WavDecoder.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace af::assets;
using af::core::ErrorCode;
using af::core::FrameCount;
using af::test::fixtureFrames;
using af::test::fixtureSample;
using af::test::readBytes;
using af::test::TempDir;
using af::test::wavFixture;
using af::test::writeBytes;

namespace
{

/// Decodes the whole file into planar channels, reading in blocks of `block` frames.
std::vector<std::vector<float>> decodeAll(IAudioReader& reader, FrameCount block = 64)
{
    const auto& info = reader.info();
    std::vector<std::vector<float>> channels(
        static_cast<std::size_t>(info.channels),
        std::vector<float>(static_cast<std::size_t>(info.frames)));
    FrameCount done = 0;
    while (true)
    {
        std::vector<float*> pointers;
        pointers.reserve(channels.size());
        for (auto& ch : channels)
        {
            pointers.push_back(ch.data() + done);
        }
        const auto want = std::min(block, info.frames - done);
        auto got = reader.read(pointers, want);
        REQUIRE(got.ok());
        if (got.value() == 0)
        {
            break;
        }
        done += got.value();
    }
    REQUIRE(done == info.frames);
    return channels;
}

std::size_t findId(const std::vector<unsigned char>& bytes, const char* id)
{
    const auto it = std::search(bytes.begin(), bytes.end(), id, id + 4);
    REQUIRE(it != bytes.end());
    return static_cast<std::size_t>(it - bytes.begin());
}

void put16(std::vector<unsigned char>& bytes, std::size_t at, unsigned value)
{
    bytes[at] = static_cast<unsigned char>(value & 0xFFu);
    bytes[at + 1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
}

void put32(std::vector<unsigned char>& bytes, std::size_t at, unsigned value)
{
    put16(bytes, at, value & 0xFFFFu);
    put16(bytes, at + 2, value >> 16);
}

af::core::Result<std::unique_ptr<IAudioReader>> openBytes(const TempDir& dir,
                                                          const std::vector<unsigned char>& bytes)
{
    const auto file = dir / "patched.wav";
    writeBytes(file, bytes);
    return DecoderRegistry::withBuiltInDecoders().open(file);
}

} // namespace

TEST_CASE("Every WAV variant decodes to the exact test signal", "[assets][wav]")
{
    struct Fixture
    {
        const char* name;
        ContainerFormat container;
        SampleFormat format;
        int rate;
        int channels;
        FrameCount frames = fixtureFrames;
    };
    const std::vector<Fixture> fixtures = {
        {"pcm_u8_mono_44100.wav", ContainerFormat::wav, SampleFormat::uint8, 44'100, 1},
        {"pcm_s16_stereo_48000.wav", ContainerFormat::wav, SampleFormat::int16, 48'000, 2},
        {"pcm_s24_stereo_96000.wav", ContainerFormat::wav, SampleFormat::int24, 96'000, 2},
        {"pcm_s32_mono_48000.wav", ContainerFormat::wav, SampleFormat::int32, 48'000, 1},
        {"float32_stereo_48000.wav", ContainerFormat::wav, SampleFormat::float32, 48'000, 2},
        {"float64_mono_48000.wav", ContainerFormat::wav, SampleFormat::float64, 48'000, 1},
        {"extensible_s24_4ch_48000.wav", ContainerFormat::wav, SampleFormat::int24, 48'000, 4},
        {"bwf_s16_stereo_48000.wav", ContainerFormat::wav, SampleFormat::int16, 48'000, 2},
        {"rf64_s24_stereo_48000.wav", ContainerFormat::rf64, SampleFormat::int24, 48'000, 2},
        {"w64_s16_stereo_48000.w64", ContainerFormat::w64, SampleFormat::int16, 48'000, 2, 302},
        {"w64_float32_mono_44100.w64", ContainerFormat::w64, SampleFormat::float32, 44'100, 1},
    };

    const auto registry = DecoderRegistry::withBuiltInDecoders();
    for (const auto& f : fixtures)
    {
        DYNAMIC_SECTION(f.name)
        {
            auto opened = registry.open(wavFixture(f.name));
            INFO((opened.ok() ? std::string{} : opened.error().describe()));
            REQUIRE(opened.ok());
            auto& reader = *opened.value();
            const auto& info = reader.info();
            REQUIRE(info.container == f.container);
            REQUIRE(info.sampleFormat == f.format);
            REQUIRE(info.sampleRate == f.rate);
            REQUIRE(info.channels == f.channels);
            REQUIRE(info.frames == f.frames);
            REQUIRE_FALSE(info.dataTruncated);
            REQUIRE(info.hasBroadcastExtension == (std::string(f.name).rfind("bwf", 0) == 0));
            REQUIRE(info.decodedBytes() == static_cast<std::uint64_t>(f.frames * f.channels * 4));

            for (const FrameCount block : {FrameCount{1}, FrameCount{64}, FrameCount{1000}})
            {
                CAPTURE(block);
                auto again = registry.open(wavFixture(f.name));
                const auto channels = decodeAll(*again.value(), block);
                for (int c = 0; c < f.channels; ++c)
                {
                    for (FrameCount i = 0; i < f.frames; ++i)
                    {
                        if (channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] !=
                            fixtureSample(i, c))
                        {
                            FAIL("sample " << i << " channel " << c << " is "
                                           << channels[static_cast<std::size_t>(c)]
                                                      [static_cast<std::size_t>(i)]);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Reading past the end returns zero frames", "[assets][wav]")
{
    auto opened =
        DecoderRegistry::withBuiltInDecoders().open(wavFixture("pcm_s16_stereo_48000.wav"));
    REQUIRE(opened.ok());
    std::vector<float> left(400);
    std::vector<float> right(400);
    std::vector<float*> pointers{left.data(), right.data()};
    REQUIRE(opened.value()->read(pointers, 400).value() == fixtureFrames);
    REQUIRE(opened.value()->read(pointers, 400).value() == 0);
}

TEST_CASE("Wrong channel buffer count is rejected", "[assets][wav]")
{
    auto opened =
        DecoderRegistry::withBuiltInDecoders().open(wavFixture("pcm_s16_stereo_48000.wav"));
    std::vector<float> mono(10);
    std::vector<float*> pointers{mono.data()};
    const auto result = opened.value()->read(pointers, 10);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::invalidArgument);
}

TEST_CASE("BW64 files decode like RF64", "[assets][wav]")
{
    const TempDir dir;
    auto bytes = readBytes(wavFixture("rf64_s24_stereo_48000.wav"));
    std::memcpy(bytes.data(), "BW64", 4);
    auto opened = openBytes(dir, bytes);
    REQUIRE(opened.ok());
    REQUIRE(opened.value()->info().container == ContainerFormat::bw64);
    const auto channels = decodeAll(*opened.value());
    REQUIRE(channels[1][300] == fixtureSample(300, 1));
}

TEST_CASE("A truncated data chunk decodes the frames that are present", "[assets][wav]")
{
    const TempDir dir;
    auto bytes = readBytes(wavFixture("pcm_s16_stereo_48000.wav"));
    const auto dataBody = findId(bytes, "data") + 8;
    bytes.resize(dataBody + std::size_t{100} * 4 + 3); // 100 whole frames and a partial one
    auto opened = openBytes(dir, bytes);
    REQUIRE(opened.ok());
    REQUIRE(opened.value()->info().dataTruncated);
    REQUIRE(opened.value()->info().frames == 100);
    const auto channels = decodeAll(*opened.value());
    REQUIRE(channels[0][99] == fixtureSample(99, 0));
}

TEST_CASE("Damaged and unsupported files give an error with a reason", "[assets][wav]")
{
    const TempDir dir;
    const auto s16 = readBytes(wavFixture("pcm_s16_stereo_48000.wav"));
    const auto fmt = findId(s16, "fmt ");
    const auto body = fmt + 8;

    struct Case
    {
        const char* name;
        std::vector<unsigned char> bytes;
        ErrorCode code;
    };
    std::vector<Case> cases;
    auto patched = [&](auto change)
    {
        auto b = s16;
        change(b);
        return b;
    };

    cases.push_back({"empty file", {}, ErrorCode::unsupported});
    cases.push_back(
        {"random bytes", std::vector<unsigned char>(100, 0x5A), ErrorCode::unsupported});
    cases.push_back({"header cut after 20 bytes",
                     std::vector<unsigned char>(s16.begin(), s16.begin() + 20),
                     ErrorCode::invalidData});
    cases.push_back({"fmt chunk missing",
                     patched([&](auto& b) { std::memcpy(b.data() + fmt, "junk", 4); }),
                     ErrorCode::invalidData});
    cases.push_back(
        {"data chunk missing",
         patched([&](auto& b) { std::memcpy(b.data() + findId(b, "data"), "junk", 4); }),
         ErrorCode::invalidData});
    cases.push_back({"no audio frames",
                     patched([&](auto& b) { put32(b, findId(b, "data") + 4, 0); }),
                     ErrorCode::invalidData});
    cases.push_back({"zero channels", patched([&](auto& b) { put16(b, body + 2, 0); }),
                     ErrorCode::invalidData});
    cases.push_back({"33 channels",
                     patched(
                         [&](auto& b)
                         {
                             put16(b, body + 2, 33);
                             put16(b, body + 12, 66);
                         }),
                     ErrorCode::unsupported});
    cases.push_back({"sample rate 0", patched([&](auto& b) { put32(b, body + 4, 0); }),
                     ErrorCode::unsupported});
    cases.push_back({"block align mismatch", patched([&](auto& b) { put16(b, body + 12, 3); }),
                     ErrorCode::invalidData});
    cases.push_back(
        {"ADPCM codec", patched([&](auto& b) { put16(b, body, 2); }), ErrorCode::unsupported});
    cases.push_back({"12-byte samples",
                     patched(
                         [&](auto& b)
                         {
                             put16(b, body + 14, 96);
                             put16(b, body + 12, 24);
                         }),
                     ErrorCode::unsupported});
    cases.push_back({"big-endian RIFX", patched([&](auto& b) { std::memcpy(b.data(), "RIFX", 4); }),
                     ErrorCode::unsupported});
    cases.push_back({"fmt chunk too short", patched([&](auto& b) { put32(b, fmt + 4, 8); }),
                     ErrorCode::invalidData});

    auto rf64 = readBytes(wavFixture("rf64_s24_stereo_48000.wav"));
    std::memcpy(rf64.data() + findId(rf64, "ds64"), "junk", 4);
    cases.push_back({"RF64 without ds64", rf64, ErrorCode::invalidData});

    for (const auto& c : cases)
    {
        DYNAMIC_SECTION(c.name)
        {
            const auto result = openBytes(dir, c.bytes);
            REQUIRE_FALSE(result.ok());
            INFO(result.error().describe());
            REQUIRE(result.error().code == c.code);
            REQUIRE_FALSE(result.error().message.empty());
            REQUIRE(result.error().subject.find("patched.wav") != std::string::npos);
        }
    }
}

TEST_CASE("A missing file is reported as not found", "[assets][wav]")
{
    const TempDir dir;
    const auto result = DecoderRegistry::withBuiltInDecoders().open(dir / "missing.wav");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::notFound);
    REQUIRE(WavDecoder{}.open(dir / "missing.wav").error().code == ErrorCode::notFound);
}
