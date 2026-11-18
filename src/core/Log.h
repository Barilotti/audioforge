#pragma once

#include "core/Result.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

/// Application log: one file per run, in a directory chosen by the application
/// (%LOCALAPPDATA%\AudioForge\Logs on Windows), with the oldest files pruned.
///
/// Thread-safe, but blocking (mutex and file I/O): never call it from the audio thread.
/// Messages written on a thread marked with ScopedRealtimeThread are dropped and counted
/// instead of blocking; the count is reported by the next regular message.
namespace af::core::log
{

enum class Level
{
    trace,
    debug,
    info,
    warning,
    error,
};

[[nodiscard]] std::string_view toString(Level level) noexcept;

struct Config
{
    std::filesystem::path directory;
    std::string filePrefix = "AudioForge";
    int maxFiles = 10; ///< Files kept, including the new one.
    Level minLevel = Level::info;
    bool alsoToStderr = false;
};

/// Opens a new log file in config.directory (created if missing) and prunes old files.
/// Returns the path of the new file. Before initialise() is called, messages go to stderr.
Result<std::filesystem::path> initialise(const Config& config);

/// Flushes and closes the log file. Later messages go to stderr.
void shutdown();

[[nodiscard]] bool isEnabled(Level level) noexcept;

void write(Level level, std::string_view message);

/// Number of messages dropped because they were written from a real-time thread.
[[nodiscard]] std::uint64_t droppedRealtimeMessages() noexcept;

template <typename... Args>
void trace(std::format_string<Args...> format, Args&&... args)
{
    if (isEnabled(Level::trace))
    {
        write(Level::trace, std::format(format, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void debug(std::format_string<Args...> format, Args&&... args)
{
    if (isEnabled(Level::debug))
    {
        write(Level::debug, std::format(format, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void info(std::format_string<Args...> format, Args&&... args)
{
    if (isEnabled(Level::info))
    {
        write(Level::info, std::format(format, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void warning(std::format_string<Args...> format, Args&&... args)
{
    if (isEnabled(Level::warning))
    {
        write(Level::warning, std::format(format, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void error(std::format_string<Args...> format, Args&&... args)
{
    if (isEnabled(Level::error))
    {
        write(Level::error, std::format(format, std::forward<Args>(args)...));
    }
}

} // namespace af::core::log
