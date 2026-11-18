#pragma once

namespace af::core
{

/// Marks the current thread as a real-time audio thread for the lifetime of the object.
/// Code that must never run on such a thread (logging, file I/O, allocation-heavy work) checks
/// isRealtimeThread() in debug builds. Construct it at the top of the audio callback.
class ScopedRealtimeThread
{
public:
    ScopedRealtimeThread() noexcept;
    ~ScopedRealtimeThread() noexcept;

    ScopedRealtimeThread(const ScopedRealtimeThread&) = delete;
    ScopedRealtimeThread& operator=(const ScopedRealtimeThread&) = delete;
    ScopedRealtimeThread(ScopedRealtimeThread&&) = delete;
    ScopedRealtimeThread& operator=(ScopedRealtimeThread&&) = delete;

private:
    bool previous_;
};

/// True while a ScopedRealtimeThread is alive on the calling thread.
[[nodiscard]] bool isRealtimeThread() noexcept;

} // namespace af::core
