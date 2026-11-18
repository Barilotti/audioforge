#pragma once

#include <filesystem>

namespace af::app
{

/// Installs process-wide handlers that record fatal errors.
///
/// Windows: unhandled exceptions and std::terminate write a minidump
/// (AudioForge_<date>-<time>_<pid>.dmp) into `dumpDirectory`, then the process ends.
/// Other platforms: fatal signals print a short message to stderr.
///
/// Dumps stay on the user's machine (no upload in phase 1, see decision L7).
/// Call once, early in start-up, before other threads are created.
void installCrashHandler(const std::filesystem::path& dumpDirectory);

/// Crashes the process on purpose. Used by the crash-reporting test (--crash-test).
[[noreturn]] void triggerTestCrash();

} // namespace af::app
