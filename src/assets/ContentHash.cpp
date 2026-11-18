#include "assets/ContentHash.h"

#define XXH_INLINE_ALL
#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>
#include <xxhash.h>

namespace af::assets
{

namespace
{

constexpr std::size_t chunkBytes = std::size_t{1} << 20;

std::string pathText(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

struct StateDeleter
{
    void operator()(XXH3_state_t* state) const noexcept { XXH3_freeState(state); }
};

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

std::string ContentHash::toHex() const
{
    std::array<char, 33> text{};
    (void)std::snprintf(text.data(), text.size(), "%016llx%016llx",
                        static_cast<unsigned long long>(high),
                        static_cast<unsigned long long>(low));
    return {text.data(), 32};
}

std::optional<ContentHash> ContentHash::fromHex(std::string_view hex)
{
    if (hex.size() != 32)
    {
        return std::nullopt;
    }
    ContentHash hash;
    for (std::size_t i = 0; i < 32; ++i)
    {
        const int v = hexValue(hex[i]);
        if (v < 0)
        {
            return std::nullopt;
        }
        auto& part = i < 16 ? hash.high : hash.low;
        part = (part << 4U) | static_cast<std::uint64_t>(v);
    }
    return hash;
}

core::Result<ContentHash> hashFile(const std::filesystem::path& file,
                                   const std::atomic<bool>* cancel)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return core::makeError(core::ErrorCode::ioError, "Cannot open the file to hash it",
                               pathText(file));
    }
    const std::unique_ptr<XXH3_state_t, StateDeleter> state{XXH3_createState()};
    if (!state || XXH3_128bits_reset(state.get()) == XXH_ERROR)
    {
        return core::makeError(core::ErrorCode::outOfMemory, "Cannot create a hash state");
    }
    std::vector<char> buffer(chunkBytes);
    while (in)
    {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
        {
            return core::makeError(core::ErrorCode::cancelled, "Hashing was cancelled");
        }
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got > 0)
        {
            (void)XXH3_128bits_update(state.get(), buffer.data(), got);
        }
    }
    if (in.bad())
    {
        return core::makeError(core::ErrorCode::ioError, "Reading the file failed while hashing",
                               pathText(file));
    }
    const auto digest = XXH3_128bits_digest(state.get());
    return ContentHash{digest.high64, digest.low64};
}

std::uint64_t hash64(const void* data, std::size_t size) noexcept
{
    return XXH3_64bits(data, size);
}

} // namespace af::assets
