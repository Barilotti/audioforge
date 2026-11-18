#include "core/RealtimeThread.h"

namespace af::core
{

namespace
{
thread_local bool realtimeThread = false;
} // namespace

ScopedRealtimeThread::ScopedRealtimeThread() noexcept
    : previous_(realtimeThread)
{
    realtimeThread = true;
}

ScopedRealtimeThread::~ScopedRealtimeThread() noexcept
{
    realtimeThread = previous_;
}

bool isRealtimeThread() noexcept
{
    return realtimeThread;
}

} // namespace af::core
