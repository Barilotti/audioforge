#pragma once

#include "assets/AudioFileInfo.h"
#include "core/Result.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace af::assets
{

/// Sequential reader of one open audio file. Not thread-safe; used by one worker at a time.
class IAudioReader
{
public:
    virtual ~IAudioReader() = default;

    [[nodiscard]] virtual const AudioFileInfo& info() const noexcept = 0;

    /// Decodes up to `maxFrames` frames into the planar float buffers `channels` (one pointer per
    /// channel of the file, each with room for maxFrames) and advances. Returns the number of
    /// frames written: less than maxFrames only at the end of the file, 0 after it.
    virtual core::Result<core::FrameCount> read(std::span<float* const> channels,
                                                core::FrameCount maxFrames) = 0;

protected:
    IAudioReader() = default;
    IAudioReader(const IAudioReader&) = default;
    IAudioReader& operator=(const IAudioReader&) = default;
    IAudioReader(IAudioReader&&) = default;
    IAudioReader& operator=(IAudioReader&&) = default;
};

/// A decoder for one family of file formats (WAV now; AIFF, FLAC, MP3 later).
class IAudioDecoder
{
public:
    virtual ~IAudioDecoder() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// True if the first bytes of a file (up to 64) look like this decoder's format.
    [[nodiscard]] virtual bool recognises(std::span<const unsigned char> header) const noexcept = 0;

    /// Opens and parses the file. Errors: notFound, ioError, invalidData (corrupt), unsupported.
    [[nodiscard]] virtual core::Result<std::unique_ptr<IAudioReader>>
    open(const std::filesystem::path& file) const = 0;

protected:
    IAudioDecoder() = default;
    IAudioDecoder(const IAudioDecoder&) = default;
    IAudioDecoder& operator=(const IAudioDecoder&) = default;
    IAudioDecoder(IAudioDecoder&&) = default;
    IAudioDecoder& operator=(IAudioDecoder&&) = default;
};

/// The decoders AudioForge knows, chosen by file content rather than by extension.
class DecoderRegistry
{
public:
    /// Registry with every built-in decoder.
    [[nodiscard]] static DecoderRegistry withBuiltInDecoders();

    void add(std::shared_ptr<const IAudioDecoder> decoder);

    /// Opens the file with the first decoder that recognises its header.
    [[nodiscard]] core::Result<std::unique_ptr<IAudioReader>>
    open(const std::filesystem::path& file) const;

private:
    std::vector<std::shared_ptr<const IAudioDecoder>> decoders_;
};

} // namespace af::assets
