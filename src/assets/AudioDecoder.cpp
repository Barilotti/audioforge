#include "assets/AudioDecoder.h"

#include "assets/WavDecoder.h"

#include <array>
#include <fstream>
#include <system_error>

namespace af::assets
{

std::string_view toString(ContainerFormat format) noexcept
{
    switch (format)
    {
    case ContainerFormat::wav:
        return "WAV";
    case ContainerFormat::rf64:
        return "RF64";
    case ContainerFormat::bw64:
        return "BW64";
    case ContainerFormat::w64:
        return "Wave64";
    }
    return "?";
}

std::string_view toString(SampleFormat format) noexcept
{
    switch (format)
    {
    case SampleFormat::uint8:
        return "8-bit PCM";
    case SampleFormat::int16:
        return "16-bit PCM";
    case SampleFormat::int24:
        return "24-bit PCM";
    case SampleFormat::int32:
        return "32-bit PCM";
    case SampleFormat::float32:
        return "32-bit float";
    case SampleFormat::float64:
        return "64-bit float";
    }
    return "?";
}

DecoderRegistry DecoderRegistry::withBuiltInDecoders()
{
    DecoderRegistry registry;
    registry.add(std::make_shared<const WavDecoder>());
    return registry;
}

void DecoderRegistry::add(std::shared_ptr<const IAudioDecoder> decoder)
{
    decoders_.push_back(std::move(decoder));
}

core::Result<std::unique_ptr<IAudioReader>>
DecoderRegistry::open(const std::filesystem::path& file) const
{
    const auto u8 = file.u8string();
    const std::string subject{reinterpret_cast<const char*>(u8.data()), u8.size()};

    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
    {
        return core::makeError(core::ErrorCode::notFound, "File not found", subject);
    }

    std::array<unsigned char, 64> header{};
    std::size_t headerBytes = 0;
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
        {
            return core::makeError(core::ErrorCode::ioError, "Cannot open file", subject);
        }
        stream.read(reinterpret_cast<char*>(header.data()),
                    static_cast<std::streamsize>(header.size()));
        headerBytes = static_cast<std::size_t>(stream.gcount());
    }

    const std::span<const unsigned char> bytes(header.data(), headerBytes);
    for (const auto& decoder : decoders_)
    {
        if (decoder->recognises(bytes))
        {
            return decoder->open(file);
        }
    }
    return core::makeError(core::ErrorCode::unsupported, "Unknown audio file format", subject);
}

} // namespace af::assets
