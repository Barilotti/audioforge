#pragma once

#include <cstdint>
#include <memory>

namespace af::assets
{

inline constexpr std::uint64_t mebibyte = std::uint64_t{1} << 20;
inline constexpr std::uint64_t gibibyte = std::uint64_t{1} << 30;

/// Memory limits of the AudioBufferCache (architecture v0.2, section 4.7; ADR-0006). All of
/// them are user settings; defaultsFor() gives the defaults for a machine.
struct MemoryBudget
{
    /// Above this, unpinned assets are evicted (least recently used first).
    std::uint64_t softBytes = 0;
    /// New decodes are refused while used memory plus the new asset would exceed this.
    std::uint64_t hardBytes = 0;
    /// A decode is refused if less than this would remain available to the whole system.
    std::uint64_t systemReserveBytes = 0;
    /// Largest single decoded asset.
    std::uint64_t maxAssetBytes = gibibyte;
    /// Decoded assets above this get a size warning in the UI.
    std::uint64_t largeAssetBytes = 256 * mebibyte;

    /// Defaults for a machine with `physicalBytes` of RAM:
    /// hard = min(50% RAM, 8 GB), soft = min(40% RAM, 80% of hard), reserve = max(1 GB, 10% RAM).
    [[nodiscard]] static MemoryBudget defaultsFor(std::uint64_t physicalBytes) noexcept;
};

/// System memory figures. An interface so tests can simulate small or busy machines.
class ISystemMemory
{
public:
    virtual ~ISystemMemory() = default;

    [[nodiscard]] virtual std::uint64_t physicalBytes() const = 0;
    /// Memory the OS can give us right now without paging out other programs.
    [[nodiscard]] virtual std::uint64_t availableBytes() const = 0;

    /// The real machine (GlobalMemoryStatusEx on Windows, /proc/meminfo on Linux).
    [[nodiscard]] static std::shared_ptr<const ISystemMemory> host();

protected:
    ISystemMemory() = default;
    ISystemMemory(const ISystemMemory&) = default;
    ISystemMemory& operator=(const ISystemMemory&) = default;
    ISystemMemory(ISystemMemory&&) = default;
    ISystemMemory& operator=(ISystemMemory&&) = default;
};

} // namespace af::assets
