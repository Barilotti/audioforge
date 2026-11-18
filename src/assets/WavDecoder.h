#pragma once

#include "assets/AudioDecoder.h"

namespace af::assets
{

/// WAV family decoder: RIFF/WAVE (PCM 8/16/24/32, float 32/64, WAVE_FORMAT_EXTENSIBLE, BWF),
/// RF64, BW64 and Wave64. Little-endian only; RIFX and compressed codecs (ADPCM, A-law, mu-law)
/// are reported as unsupported.
class WavDecoder final : public IAudioDecoder
{
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "WAV"; }
    [[nodiscard]] bool recognises(std::span<const unsigned char> header) const noexcept override;
    [[nodiscard]] core::Result<std::unique_ptr<IAudioReader>>
    open(const std::filesystem::path& file) const override;
};

} // namespace af::assets
