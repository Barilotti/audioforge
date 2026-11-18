#include "assets/MemoryBudget.h"

#include <algorithm>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fstream>
#include <string>
#include <unistd.h>
#endif

namespace af::assets
{

MemoryBudget MemoryBudget::defaultsFor(std::uint64_t physicalBytes) noexcept
{
    constexpr std::uint64_t cap = 8 * gibibyte;
    MemoryBudget budget;
    budget.hardBytes = std::min(physicalBytes / 2, cap);
    budget.softBytes = std::min(physicalBytes / 5 * 2, budget.hardBytes / 5 * 4);
    budget.systemReserveBytes = std::max(gibibyte, physicalBytes / 10);
    return budget;
}

namespace
{

class HostMemory final : public ISystemMemory
{
public:
#if defined(_WIN32)
    [[nodiscard]] std::uint64_t physicalBytes() const override
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof status;
        return GlobalMemoryStatusEx(&status) != 0 ? status.ullTotalPhys : 0;
    }

    [[nodiscard]] std::uint64_t availableBytes() const override
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof status;
        return GlobalMemoryStatusEx(&status) != 0 ? status.ullAvailPhys : 0;
    }
#else
    [[nodiscard]] std::uint64_t physicalBytes() const override
    {
        const long pages = sysconf(_SC_PHYS_PAGES);
        const long pageSize = sysconf(_SC_PAGESIZE);
        return pages > 0 && pageSize > 0
                   ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize)
                   : 0;
    }

    [[nodiscard]] std::uint64_t availableBytes() const override
    {
        std::ifstream meminfo("/proc/meminfo");
        std::string key;
        std::uint64_t kib = 0;
        std::string unit;
        while (meminfo >> key >> kib >> unit)
        {
            if (key == "MemAvailable:")
            {
                return kib * 1024;
            }
        }
        return physicalBytes();
    }
#endif
};

} // namespace

std::shared_ptr<const ISystemMemory> ISystemMemory::host()
{
    static const auto instance = std::make_shared<const HostMemory>();
    return instance;
}

} // namespace af::assets
