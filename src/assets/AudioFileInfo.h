#pragma once

#include "core/TimeTicks.h"

#include <cstdint>
#include <string_view>

namespace af::assets
{

/// Largest channel count AudioForge imports (architecture v0.2, section 4.7).
inline constexpr int maxChannels = 32;

enum class ContainerFormat
{
    wav,  ///< RIFF/WAVE, including BWF (a WAVE file with a bext chunk).
    rf64, ///< EBU RF64: RIFF with 64-bit sizes in a ds64 chunk.
    bw64, ///< ITU-R BS.2088 BW64: same layout as RF64.
    w64,  ///< Sony Wave64: GUID chunk ids and 64-bit sizes.
};

enum class SampleFormat
{
    uint8,   ///< Unsigned 8-bit PCM.
    int16,   ///< Signed 16-bit PCM.
    int24,   ///< Signed 24-bit PCM in 3 bytes.
    int32,   ///< Signed 32-bit PCM.
    float32, ///< IEEE float.
    float64, ///< IEEE double.
};

[[nodiscard]] std::string_view toString(ContainerFormat format) noexcept;
[[nodiscard]] std::string_view toString(SampleFormat format) noexcept;

/// Properties of an audio file, known after its header is parsed.
struct AudioFileInfo
{
    ContainerFormat container = ContainerFormat::wav;
    SampleFormat sampleFormat = SampleFormat::int16;
    int validBitsPerSample = 16; ///< May be less than the container size, e.g. 20 bits in 24.
    core::SampleRate sampleRate = 0;
    int channels = 0;
    core::FrameCount frames = 0;
    std::uint32_t channelMask = 0;      ///< WAVE_FORMAT_EXTENSIBLE speaker mask, 0 when absent.
    bool hasBroadcastExtension = false; ///< BWF bext chunk present.

    /// The data chunk claimed more bytes than the file holds (typical for a recorder that lost
    /// power). The frames that are present are decoded; the UI should tell the user.
    bool dataTruncated = false;

    /// Memory needed for the decoded float32 audio.
    [[nodiscard]] std::uint64_t decodedBytes() const noexcept
    {
        return static_cast<std::uint64_t>(frames) * static_cast<std::uint64_t>(channels) *
               sizeof(float);
    }
};

} // namespace af::assets
