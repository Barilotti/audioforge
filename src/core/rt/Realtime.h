#pragma once

#include <cstddef>
#include <new>

/// Marks a function as real-time safe: no allocation, no locks, no blocking system calls.
/// With Clang 20+ this is [[clang::nonblocking]], which RealtimeSanitizer checks at run time
/// (the RTSan CI job) and the compiler checks statically where it can. Elsewhere it is empty.
/// Usage: `void process() noexcept AF_NONBLOCKING;` (it follows the declarator).
#if defined(__clang__) && __clang_major__ >= 20
#define AF_NONBLOCKING [[clang::nonblocking]]
#else
#define AF_NONBLOCKING
#endif

namespace af::core::rt
{

/// Size used to keep data written by different threads on different cache lines.
/// A fixed 64 avoids the ABI warning of std::hardware_destructive_interference_size.
inline constexpr std::size_t cacheLineSize = 64;

} // namespace af::core::rt
