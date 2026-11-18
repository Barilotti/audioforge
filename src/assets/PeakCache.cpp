#include "assets/PeakCache.h"

#include "core/Uuid.h"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>

namespace af::assets
{

static_assert(std::endian::native == std::endian::little,
              "The .afpk format is little-endian; add byte swapping for this platform");

namespace
{

constexpr std::array<char, 4> magic{'A', 'F', 'P', 'K'};

// Fixed-size header, written field by field (no struct padding in the file).
struct Header
{
    std::uint32_t version = PeakCache::formatVersion;
    std::int64_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint32_t levels = PeakData::levelCount;
    std::int64_t frames = 0;
    std::array<std::int64_t, PeakData::levelCount> blockFrames{};
};

class Writer
{
public:
    template <typename T>
    void put(const T& value)
    {
        const auto* p = reinterpret_cast<const char*>(&value);
        bytes_.insert(bytes_.end(), p, p + sizeof(T));
    }
    void putBytes(const void* data, std::size_t size)
    {
        const auto* p = static_cast<const char*>(data);
        bytes_.insert(bytes_.end(), p, p + size);
    }
    [[nodiscard]] const std::vector<char>& bytes() const noexcept { return bytes_; }

private:
    std::vector<char> bytes_;
};

class Reader
{
public:
    explicit Reader(const std::vector<char>& bytes)
        : bytes_(bytes)
    {
    }
    template <typename T>
    bool get(T& value)
    {
        return getBytes(&value, sizeof(T));
    }
    bool getBytes(void* out, std::size_t size)
    {
        if (bytes_.size() - position_ < size)
        {
            return false;
        }
        std::memcpy(out, bytes_.data() + position_, size);
        position_ += size;
        return true;
    }
    [[nodiscard]] std::size_t position() const noexcept { return position_; }

private:
    const std::vector<char>& bytes_;
    std::size_t position_ = 0;
};

} // namespace

PeakCache::PeakCache(std::filesystem::path directory)
    : directory_(std::move(directory))
{
}

std::filesystem::path PeakCache::fileFor(const ContentHash& hash) const
{
    return directory_ / (hash.toHex() + ".afpk");
}

PeakDataPtr PeakCache::load(const ContentHash& hash) const
{
    std::ifstream in(fileFor(hash), std::ios::binary);
    if (!in)
    {
        return nullptr;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>()};
    if (bytes.size() < magic.size() + sizeof(std::uint64_t))
    {
        return nullptr;
    }
    const auto payload = bytes.size() - sizeof(std::uint64_t);
    std::uint64_t checksum = 0;
    std::memcpy(&checksum, bytes.data() + payload, sizeof checksum);
    if (checksum != hash64(bytes.data(), payload))
    {
        return nullptr; // damaged or cut short
    }

    Reader reader{bytes};
    std::array<char, 4> fileMagic{};
    Header h;
    if (!reader.get(fileMagic) || fileMagic != magic || !reader.get(h.version) ||
        h.version != formatVersion || !reader.get(h.sampleRate) || !reader.get(h.channels) ||
        !reader.get(h.levels) || h.levels != PeakData::levelCount || !reader.get(h.frames) ||
        !reader.get(h.blockFrames) || h.blockFrames != PeakData::blockFrames || h.channels == 0 ||
        h.channels > 64 || h.frames < 0 || h.sampleRate <= 0)
    {
        return nullptr;
    }
    try
    {
        auto peaks =
            std::make_shared<PeakData>(h.sampleRate, static_cast<int>(h.channels), h.frames);
        const auto storage = peaks->writableStorage();
        if (payload - reader.position() != storage.size_bytes() ||
            !reader.getBytes(storage.data(), storage.size_bytes()))
        {
            return nullptr;
        }
        return peaks;
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

core::Result<void> PeakCache::store(const ContentHash& hash, const PeakData& peaks) const
{
    Writer w;
    w.put(magic);
    w.put(formatVersion);
    w.put(static_cast<std::int64_t>(peaks.sampleRate()));
    w.put(static_cast<std::uint32_t>(peaks.channels()));
    w.put(static_cast<std::uint32_t>(PeakData::levelCount));
    w.put(static_cast<std::int64_t>(peaks.frames()));
    w.put(PeakData::blockFrames);
    const auto storage = peaks.storage();
    w.putBytes(storage.data(), storage.size_bytes());
    const auto checksum = hash64(w.bytes().data(), w.bytes().size());
    w.put(checksum);

    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    const auto target = fileFor(hash);
    // A unique temporary name: two workers may store the same content at the same time.
    const auto temp =
        directory_ / (hash.toHex() + "." + core::Uuid::generate().toString() + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(w.bytes().data(), static_cast<std::streamsize>(w.bytes().size()));
        if (!out)
        {
            std::filesystem::remove(temp, ec);
            return core::makeError(core::ErrorCode::ioError, "Cannot write the peak cache file",
                                   temp.string());
        }
    }
    std::filesystem::rename(temp, target, ec);
    if (ec)
    {
        std::filesystem::remove(temp, ec);
        return core::makeError(core::ErrorCode::ioError,
                               "Cannot move the peak cache file into place", target.string());
    }
    return {};
}

} // namespace af::assets
