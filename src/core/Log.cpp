#include "core/Log.h"

#include "core/RealtimeThread.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

namespace af::core::log
{

namespace
{

struct LoggerState
{
    std::mutex mutex;
    std::ofstream file;
    bool alsoToStderr = true;
    std::uint64_t reportedDrops = 0;
};

LoggerState& state()
{
    static LoggerState instance;
    return instance;
}

std::atomic<int> minimumLevel{static_cast<int>(Level::info)};
std::atomic<std::uint64_t> droppedMessages{0};

std::string formatLine(Level level, std::string_view message)
{
    const auto now =
        std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const auto threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::format("{:%Y-%m-%d %H:%M:%S}Z [{:<7}] [{:04x}] {}\n", now, toString(level),
                       threadHash & 0xFFFF, message);
}

void emitLocked(LoggerState& s, const std::string& line)
{
    if (s.file.is_open())
    {
        s.file << line;
        s.file.flush();
    }
    if (s.alsoToStderr || !s.file.is_open())
    {
        std::fputs(line.c_str(), stderr);
    }
}

void pruneOldFiles(const std::filesystem::path& directory, const std::string& prefix, int keep)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
    {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file(ec) && name.starts_with(prefix + "_") && name.ends_with(".log"))
        {
            files.push_back(entry.path());
        }
    }
    if (std::cmp_less_equal(files.size(), keep))
    {
        return;
    }
    // File names start with a sortable UTC timestamp, so name order is age order.
    std::sort(files.begin(), files.end());
    const auto excess = files.size() - static_cast<std::size_t>(keep);
    for (std::size_t i = 0; i < excess; ++i)
    {
        std::filesystem::remove(files[i], ec);
    }
}

} // namespace

std::string_view toString(Level level) noexcept
{
    switch (level)
    {
    case Level::trace:
        return "TRACE";
    case Level::debug:
        return "DEBUG";
    case Level::info:
        return "INFO";
    case Level::warning:
        return "WARNING";
    case Level::error:
        return "ERROR";
    }
    return "UNKNOWN";
}

Result<std::filesystem::path> initialise(const Config& config)
{
    if (config.directory.empty() || config.filePrefix.empty() || config.maxFiles < 1)
    {
        return makeError(ErrorCode::invalidArgument, "Invalid log configuration");
    }

    std::error_code ec;
    std::filesystem::create_directories(config.directory, ec);
    if (ec)
    {
        return makeError(ErrorCode::ioError, "Cannot create log directory: " + ec.message(),
                         config.directory.string());
    }

    const auto now =
        std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const auto stem = std::format("{}_{:%Y%m%d-%H%M%S}", config.filePrefix, now);
    auto path = config.directory / (stem + ".log");
    for (int suffix = 2; std::filesystem::exists(path, ec); ++suffix)
    {
        path = config.directory / std::format("{}-{}.log", stem, suffix);
    }

    // Keep maxFiles - 1 old files: the new file is the last one.
    pruneOldFiles(config.directory, config.filePrefix, config.maxFiles - 1);

    auto& s = state();
    {
        const std::scoped_lock lock{s.mutex};
        if (s.file.is_open())
        {
            s.file.close();
        }
        s.file.open(path, std::ios::out | std::ios::trunc);
        if (!s.file.is_open())
        {
            return makeError(ErrorCode::ioError, "Cannot open log file", path.string());
        }
        s.alsoToStderr = config.alsoToStderr;
    }
    minimumLevel.store(static_cast<int>(config.minLevel), std::memory_order_relaxed);
    return path;
}

void shutdown()
{
    auto& s = state();
    const std::scoped_lock lock{s.mutex};
    if (s.file.is_open())
    {
        s.file.flush();
        s.file.close();
    }
    s.alsoToStderr = true;
}

bool isEnabled(Level level) noexcept
{
    return static_cast<int>(level) >= minimumLevel.load(std::memory_order_relaxed);
}

std::uint64_t droppedRealtimeMessages() noexcept
{
    return droppedMessages.load(std::memory_order_relaxed);
}

void write(Level level, std::string_view message)
{
    if (isRealtimeThread())
    {
        droppedMessages.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto& s = state();
    const std::string line = formatLine(level, message);
    const std::scoped_lock lock{s.mutex};

    const auto dropped = droppedMessages.load(std::memory_order_relaxed);
    if (dropped != s.reportedDrops)
    {
        emitLocked(s, formatLine(Level::warning,
                                 std::format("{} log message(s) dropped: written from a real-time "
                                             "thread",
                                             dropped - s.reportedDrops)));
        s.reportedDrops = dropped;
    }
    emitLocked(s, line);
}

} // namespace af::core::log
