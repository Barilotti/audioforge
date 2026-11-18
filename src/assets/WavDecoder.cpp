#include "assets/WavDecoder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace af::assets
{

namespace
{

using core::ErrorCode;
using core::FrameCount;
using core::makeError;
using Guid = std::array<unsigned char, 16>;

// Wave64 GUIDs as stored in the file (first three fields little-endian).
constexpr Guid w64Riff{'r',  'i',  'f',  'f',  0x2E, 0x91, 0xCF, 0x11,
                       0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00};
constexpr Guid w64Wave{'w',  'a',  'v',  'e',  0xF3, 0xAC, 0xD3, 0x11,
                       0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A};
constexpr Guid w64Fmt{'f',  'm',  't',  ' ',  0xF3, 0xAC, 0xD3, 0x11,
                      0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A};
constexpr Guid w64Data{'d',  'a',  't',  'a',  0xF3, 0xAC, 0xD3, 0x11,
                       0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A};
constexpr Guid w64Fact{'f',  'a',  'c',  't',  0xF3, 0xAC, 0xD3, 0x11,
                       0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A};
constexpr Guid w64Bext{'b',  'e',  'x',  't',  0xF3, 0xAC, 0xD3, 0x11,
                       0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A};

// Tail of the KSDATAFORMAT_SUBTYPE GUIDs; the first two bytes hold the format tag.
constexpr std::array<unsigned char, 14> subtypeTail{0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                                    0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

constexpr std::uint16_t formatPcm = 0x0001;
constexpr std::uint16_t formatFloat = 0x0003;
constexpr std::uint16_t formatExtensible = 0xFFFE;
constexpr std::uint32_t sizeInDs64 = 0xFFFFFFFFu;

std::uint16_t le16(const unsigned char* p) noexcept
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t le32(const unsigned char* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t le64(const unsigned char* p) noexcept
{
    return static_cast<std::uint64_t>(le32(p)) | (static_cast<std::uint64_t>(le32(p + 4)) << 32);
}

bool hasId(const unsigned char* p, std::string_view id) noexcept
{
    return std::memcmp(p, id.data(), 4) == 0;
}

struct Format
{
    SampleFormat sampleFormat = SampleFormat::int16;
    int bytesPerSample = 2;
    int validBits = 16;
    int channels = 0;
    std::int64_t sampleRate = 0;
    std::uint32_t channelMask = 0;
};

class File
{
public:
    explicit File(const std::filesystem::path& path)
        : stream_(path, std::ios::binary)
    {
    }

    [[nodiscard]] bool isOpen() const { return stream_.is_open(); }

    bool readAt(std::uint64_t offset, void* out, std::size_t bytes)
    {
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset));
        stream_.read(static_cast<char*>(out), static_cast<std::streamsize>(bytes));
        return static_cast<std::size_t>(stream_.gcount()) == bytes;
    }

    std::size_t readSome(std::uint64_t offset, void* out, std::size_t bytes)
    {
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset));
        stream_.read(static_cast<char*>(out), static_cast<std::streamsize>(bytes));
        return static_cast<std::size_t>(stream_.gcount());
    }

private:
    std::ifstream stream_;
};

std::string pathText(const std::filesystem::path& file)
{
    const auto u8 = file.u8string();
    return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

core::Result<Format> parseFormat(const unsigned char* p, std::uint64_t size,
                                 const std::filesystem::path& file)
{
    const auto subject = pathText(file);
    if (size < 16)
    {
        return makeError(ErrorCode::invalidData, "fmt chunk is too short", subject);
    }

    std::uint16_t tag = le16(p);
    Format f;
    f.channels = le16(p + 2);
    f.sampleRate = le32(p + 4);
    const int blockAlign = le16(p + 12);
    const int bits = le16(p + 14);
    f.validBits = bits;

    if (tag == formatExtensible)
    {
        if (size < 40 || le16(p + 16) < 22)
        {
            return makeError(ErrorCode::invalidData,
                             "WAVE_FORMAT_EXTENSIBLE fmt chunk is too short", subject);
        }
        f.validBits = le16(p + 18);
        f.channelMask = le32(p + 20);
        if (std::memcmp(p + 26, subtypeTail.data(), subtypeTail.size()) != 0)
        {
            return makeError(ErrorCode::unsupported, "Unknown WAVE_FORMAT_EXTENSIBLE sub-format",
                             subject);
        }
        tag = le16(p + 24);
        if (f.validBits == 0)
        {
            f.validBits = bits;
        }
    }

    if (f.channels < 1)
    {
        return makeError(ErrorCode::invalidData, "File has no channels", subject);
    }
    if (f.channels > maxChannels)
    {
        return makeError(ErrorCode::unsupported, "More than 32 channels", subject);
    }
    if (!core::isSupportedSampleRate(f.sampleRate))
    {
        return makeError(ErrorCode::unsupported,
                         "Sample rate " + std::to_string(f.sampleRate) + " Hz is not supported",
                         subject);
    }

    f.bytesPerSample = (bits + 7) / 8;
    if (tag == formatPcm)
    {
        switch (f.bytesPerSample)
        {
        case 1:
            f.sampleFormat = SampleFormat::uint8;
            break;
        case 2:
            f.sampleFormat = SampleFormat::int16;
            break;
        case 3:
            f.sampleFormat = SampleFormat::int24;
            break;
        case 4:
            f.sampleFormat = SampleFormat::int32;
            break;
        default:
            return makeError(ErrorCode::unsupported,
                             std::to_string(bits) + "-bit PCM is not supported", subject);
        }
    }
    else if (tag == formatFloat)
    {
        if (bits == 32)
        {
            f.sampleFormat = SampleFormat::float32;
        }
        else if (bits == 64)
        {
            f.sampleFormat = SampleFormat::float64;
        }
        else
        {
            return makeError(ErrorCode::unsupported,
                             std::to_string(bits) + "-bit float is not supported", subject);
        }
    }
    else
    {
        return makeError(
            ErrorCode::unsupported,
            "WAV codec " + std::to_string(tag) + " is not supported (only PCM and float)", subject);
    }

    if (bits == 0 || f.validBits > bits)
    {
        return makeError(ErrorCode::invalidData, "Invalid bits per sample", subject);
    }
    if (blockAlign != f.channels * f.bytesPerSample)
    {
        return makeError(ErrorCode::invalidData,
                         "Block alignment does not match channels and sample size", subject);
    }
    return f;
}

class WavReader final : public IAudioReader
{
public:
    WavReader(File&& file, AudioFileInfo info, Format format, std::uint64_t dataOffset)
        : file_(std::move(file))
        , info_(info)
        , format_(format)
        , dataOffset_(dataOffset)
    {
    }

    [[nodiscard]] const AudioFileInfo& info() const noexcept override { return info_; }

    core::Result<FrameCount> read(std::span<float* const> channels, FrameCount maxFrames) override
    {
        if (static_cast<int>(channels.size()) != info_.channels || maxFrames < 0)
        {
            return makeError(ErrorCode::invalidArgument,
                             "Channel buffer count does not match the file");
        }

        const auto frameBytes = static_cast<std::size_t>(format_.bytesPerSample) *
                                static_cast<std::size_t>(format_.channels);
        FrameCount written = 0;
        while (written < maxFrames && position_ < info_.frames)
        {
            const auto chunkFrames = std::min<FrameCount>(
                {maxFrames - written, info_.frames - position_, chunkFramesMax(frameBytes)});
            const auto bytes = static_cast<std::size_t>(chunkFrames) * frameBytes;
            scratch_.resize(bytes);
            const auto offset = dataOffset_ + static_cast<std::uint64_t>(position_) * frameBytes;
            if (!file_.readAt(offset, scratch_.data(), bytes))
            {
                return makeError(ErrorCode::ioError,
                                 "Unexpected end of file while reading audio data");
            }
            convert(channels, written, chunkFrames);
            written += chunkFrames;
            position_ += chunkFrames;
        }
        return written;
    }

private:
    static FrameCount chunkFramesMax(std::size_t frameBytes) noexcept
    {
        constexpr std::size_t targetBytes = std::size_t{256} * 1024;
        return static_cast<FrameCount>(std::max<std::size_t>(1, targetBytes / frameBytes));
    }

    void convert(std::span<float* const> channels, FrameCount outOffset,
                 FrameCount frames) const noexcept
    {
        const auto stride = static_cast<std::size_t>(format_.bytesPerSample) *
                            static_cast<std::size_t>(format_.channels);
        const auto sampleBytes = static_cast<std::size_t>(format_.bytesPerSample);
        for (std::size_t ch = 0; ch < channels.size(); ++ch)
        {
            float* out = channels[ch] + outOffset;
            const unsigned char* in = scratch_.data() + ch * sampleBytes;
            for (FrameCount i = 0; i < frames; ++i, in += stride)
            {
                out[i] = decodeSample(in);
            }
        }
    }

    [[nodiscard]] float decodeSample(const unsigned char* p) const noexcept
    {
        switch (format_.sampleFormat)
        {
        case SampleFormat::uint8:
            return static_cast<float>(static_cast<int>(p[0]) - 128) / 128.0f;
        case SampleFormat::int16:
            return static_cast<float>(static_cast<std::int16_t>(le16(p))) / 32768.0f;
        case SampleFormat::int24:
        {
            // Place the 24 bits at the top of an int32 and shift back to sign-extend.
            const auto raw = static_cast<std::uint32_t>(p[0]) << 8 |
                             static_cast<std::uint32_t>(p[1]) << 16 |
                             static_cast<std::uint32_t>(p[2]) << 24;
            return static_cast<float>(static_cast<std::int32_t>(raw) >> 8) / 8388608.0f;
        }
        case SampleFormat::int32:
            return static_cast<float>(static_cast<double>(static_cast<std::int32_t>(le32(p))) /
                                      2147483648.0);
        case SampleFormat::float32:
        {
            const std::uint32_t bits = le32(p);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof value);
            return value;
        }
        case SampleFormat::float64:
        {
            const std::uint64_t bits = le64(p);
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof value);
            return static_cast<float>(value);
        }
        }
        return 0.0f;
    }

    File file_;
    AudioFileInfo info_;
    Format format_;
    std::uint64_t dataOffset_ = 0;
    FrameCount position_ = 0;
    std::vector<unsigned char> scratch_;
};

struct Chunks
{
    std::optional<std::vector<unsigned char>> fmt;
    std::optional<std::uint64_t> dataOffset;
    std::uint64_t dataSize = 0;
    bool dataTruncated = false;
    bool hasBext = false;
    std::optional<std::uint64_t> factFrames; // Wave64 only, see scanW64
};

constexpr std::uint64_t maxFmtSize = 4096;

core::Result<void> readFmt(File& file, std::uint64_t offset, std::uint64_t size, Chunks& chunks,
                           const std::string& subject)
{
    if (size > maxFmtSize)
    {
        return makeError(ErrorCode::invalidData, "fmt chunk is too large", subject);
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!file.readAt(offset, bytes.data(), bytes.size()))
    {
        return makeError(ErrorCode::invalidData, "fmt chunk is cut off", subject);
    }
    chunks.fmt = std::move(bytes);
    return {};
}

/// Walks RIFF/RF64/BW64 chunks. `ds64DataSize` replaces a data size of 0xFFFFFFFF.
core::Result<Chunks> scanRiff(File& file, std::uint64_t fileSize, bool is64,
                              const std::string& subject)
{
    Chunks chunks;
    std::optional<std::uint64_t> ds64DataSize;
    std::uint64_t pos = 12;
    while (pos + 8 <= fileSize)
    {
        std::array<unsigned char, 8> header{};
        if (!file.readAt(pos, header.data(), header.size()))
        {
            break;
        }
        const std::uint64_t bodyOffset = pos + 8;
        std::uint64_t size = le32(header.data() + 4);

        if (hasId(header.data(), "ds64"))
        {
            std::array<unsigned char, 24> ds64{};
            if (!is64 || size < 24 || !file.readAt(bodyOffset, ds64.data(), ds64.size()))
            {
                return makeError(ErrorCode::invalidData, "Invalid ds64 chunk", subject);
            }
            ds64DataSize = le64(ds64.data() + 8);
        }
        else if (hasId(header.data(), "fmt "))
        {
            if (auto r = readFmt(file, bodyOffset, size, chunks, subject); !r)
            {
                return r.error();
            }
        }
        else if (hasId(header.data(), "bext"))
        {
            chunks.hasBext = true;
        }
        else if (hasId(header.data(), "data"))
        {
            if (is64 && size == sizeInDs64)
            {
                if (!ds64DataSize)
                {
                    return makeError(ErrorCode::invalidData,
                                     "RF64 data size is missing (no ds64 chunk)", subject);
                }
                size = *ds64DataSize;
            }
            chunks.dataOffset = bodyOffset;
            const std::uint64_t available = fileSize - bodyOffset;
            if (size > available)
            {
                chunks.dataTruncated = true;
                size = available;
            }
            chunks.dataSize = size;
            if (chunks.dataTruncated)
            {
                break; // Nothing reliable follows a truncated data chunk.
            }
        }

        if (size > fileSize - bodyOffset)
        {
            break; // A trailing chunk that runs past the end: ignore it.
        }
        pos = bodyOffset + size + (size & 1u); // chunks are padded to an even size
    }
    return chunks;
}

/// Walks Wave64 chunks: 16-byte GUID, 64-bit size including the 24-byte header, 8-byte alignment.
core::Result<Chunks> scanW64(File& file, std::uint64_t fileSize, const std::string& subject)
{
    Chunks chunks;
    std::uint64_t pos = 40;
    while (pos + 24 <= fileSize)
    {
        std::array<unsigned char, 24> header{};
        if (!file.readAt(pos, header.data(), header.size()))
        {
            break;
        }
        const std::uint64_t total = le64(header.data() + 16);
        if (total < 24)
        {
            return makeError(ErrorCode::invalidData, "Invalid Wave64 chunk size", subject);
        }
        std::uint64_t size = total - 24;
        const std::uint64_t bodyOffset = pos + 24;

        if (std::memcmp(header.data(), w64Fmt.data(), 16) == 0)
        {
            if (auto r = readFmt(file, bodyOffset, size, chunks, subject); !r)
            {
                return r.error();
            }
        }
        else if (std::memcmp(header.data(), w64Fact.data(), 16) == 0)
        {
            std::array<unsigned char, 8> count{};
            if (size >= 8 && file.readAt(bodyOffset, count.data(), count.size()))
            {
                chunks.factFrames = le64(count.data());
            }
        }
        else if (std::memcmp(header.data(), w64Bext.data(), 16) == 0)
        {
            chunks.hasBext = true;
        }
        else if (std::memcmp(header.data(), w64Data.data(), 16) == 0)
        {
            chunks.dataOffset = bodyOffset;
            const std::uint64_t available = fileSize - bodyOffset;
            if (size > available)
            {
                chunks.dataTruncated = true;
                size = available;
            }
            chunks.dataSize = size;
            if (chunks.dataTruncated)
            {
                break;
            }
        }

        if (size > fileSize - bodyOffset)
        {
            break;
        }
        pos = bodyOffset + ((size + 7) & ~std::uint64_t{7});
    }
    return chunks;
}

} // namespace

bool WavDecoder::recognises(std::span<const unsigned char> header) const noexcept
{
    if (header.size() >= 12 && hasId(header.data() + 8, "WAVE") &&
        (hasId(header.data(), "RIFF") || hasId(header.data(), "RF64") ||
         hasId(header.data(), "BW64") || hasId(header.data(), "RIFX")))
    {
        return true;
    }
    return header.size() >= 40 && std::memcmp(header.data(), w64Riff.data(), 16) == 0 &&
           std::memcmp(header.data() + 24, w64Wave.data(), 16) == 0;
}

core::Result<std::unique_ptr<IAudioReader>>
WavDecoder::open(const std::filesystem::path& path) const
{
    const auto subject = pathText(path);
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec)
    {
        const bool missing = !std::filesystem::exists(path, ec);
        return makeError(missing ? ErrorCode::notFound : ErrorCode::ioError,
                         missing ? "File not found" : "Cannot read file size", subject);
    }

    File file{path};
    if (!file.isOpen())
    {
        return makeError(ErrorCode::ioError, "Cannot open file", subject);
    }

    std::array<unsigned char, 40> header{};
    const auto headerBytes = file.readSome(0, header.data(), header.size());
    if (headerBytes < 12 || !recognises(std::span<const unsigned char>(header.data(), headerBytes)))
    {
        return makeError(ErrorCode::invalidData, "Not a WAV file", subject);
    }

    AudioFileInfo info;
    core::Result<Chunks> scanned = Chunks{};
    if (hasId(header.data(), "RIFX"))
    {
        return makeError(ErrorCode::unsupported, "Big-endian WAV (RIFX) is not supported", subject);
    }
    if (hasId(header.data(), "RIFF"))
    {
        info.container = ContainerFormat::wav;
        scanned = scanRiff(file, fileSize, false, subject);
    }
    else if (hasId(header.data(), "RF64") || hasId(header.data(), "BW64"))
    {
        info.container =
            hasId(header.data(), "RF64") ? ContainerFormat::rf64 : ContainerFormat::bw64;
        scanned = scanRiff(file, fileSize, true, subject);
    }
    else
    {
        info.container = ContainerFormat::w64;
        scanned = scanW64(file, fileSize, subject);
    }
    if (!scanned)
    {
        return scanned.error();
    }
    const auto& chunks = scanned.value();

    if (!chunks.fmt)
    {
        return makeError(ErrorCode::invalidData, "fmt chunk is missing", subject);
    }
    if (!chunks.dataOffset)
    {
        return makeError(ErrorCode::invalidData, "data chunk is missing", subject);
    }

    auto format = parseFormat(chunks.fmt->data(), chunks.fmt->size(), path);
    if (!format)
    {
        return format.error();
    }
    const auto& f = format.value();

    const auto frameBytes =
        static_cast<std::uint64_t>(f.bytesPerSample) * static_cast<std::uint64_t>(f.channels);
    info.sampleFormat = f.sampleFormat;
    info.validBitsPerSample = f.validBits;
    info.sampleRate = f.sampleRate;
    info.channels = f.channels;
    info.channelMask = f.channelMask;
    info.frames = static_cast<FrameCount>(chunks.dataSize / frameBytes);
    if (chunks.factFrames && *chunks.factFrames < static_cast<std::uint64_t>(info.frames))
    {
        // Some Wave64 writers (ffmpeg among them) count the 8-byte alignment padding in the data
        // size; the fact chunk then holds the true length.
        info.frames = static_cast<FrameCount>(*chunks.factFrames);
    }
    info.hasBroadcastExtension = chunks.hasBext;
    info.dataTruncated = chunks.dataTruncated;

    if (info.frames == 0)
    {
        return makeError(ErrorCode::invalidData, "File contains no audio", subject);
    }

    return std::unique_ptr<IAudioReader>(
        std::make_unique<WavReader>(std::move(file), info, f, *chunks.dataOffset));
}

} // namespace af::assets
