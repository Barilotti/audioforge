#include "app/CrashReporting.h"

#include <cstdlib>
#include <exception>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h must follow windows.h.
#include <array>
#include <cwchar>
#include <dbghelp.h>
#else
#include <csignal>
#include <cstring>
#include <unistd.h>
#endif

namespace af::app
{

#if defined(_WIN32)

namespace
{

// Prepared at installation time: the handler runs in a broken process and must not allocate.
std::array<wchar_t, 1024> dumpDirectory{};

void writeMiniDump(EXCEPTION_POINTERS* exception) noexcept
{
    SYSTEMTIME time{};
    GetLocalTime(&time);

    std::array<wchar_t, 1200> path{};
    std::swprintf(path.data(), path.size(), L"%ls\\AudioForge_%04u%02u%02u-%02u%02u%02u_%lu.dmp",
                  dumpDirectory.data(), time.wYear, time.wMonth, time.wDay, time.wHour,
                  time.wMinute, time.wSecond, GetCurrentProcessId());

    const HANDLE file = CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;

    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                                 MiniDumpScanMemory | MiniDumpWithThreadInfo);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                      exception != nullptr ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* exception)
{
    writeMiniDump(exception);
    return EXCEPTION_EXECUTE_HANDLER; // terminate the process
}

[[noreturn]] void onTerminate() noexcept
{
    writeMiniDump(nullptr);
    std::_Exit(3);
}

} // namespace

void installCrashHandler(const std::filesystem::path& directory)
{
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const auto& native = directory.native();
    const auto length = std::min(native.size(), dumpDirectory.size() - 1);
    std::wmemcpy(dumpDirectory.data(), native.c_str(), length);
    dumpDirectory[length] = L'\0';

    SetUnhandledExceptionFilter(onUnhandledException);
    std::set_terminate(onTerminate);
}

void triggerTestCrash()
{
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    std::abort(); // not reached
}

#else

namespace
{

extern "C" void onFatalSignal(int signalNumber)
{
    constexpr char message[] = "AudioForge: fatal signal, terminating\n";
    [[maybe_unused]] const auto written = ::write(STDERR_FILENO, message, sizeof(message) - 1);
    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
}

} // namespace

void installCrashHandler(const std::filesystem::path& /*directory*/)
{
    for (const int signalNumber : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
    {
        std::signal(signalNumber, onFatalSignal);
    }
}

void triggerTestCrash()
{
    std::raise(SIGSEGV);
    std::abort(); // not reached
}

#endif

} // namespace af::app
