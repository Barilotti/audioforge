// Proves that RealtimeSanitizer is active in the RTSan build: allocating inside a function
// marked [[clang::nonblocking]] must abort the process. The test is registered with WILL_FAIL,
// so a build where the sanitizer silently does nothing fails CI.

#include <cstdio>
#include <vector>

namespace
{

// The attribute belongs to the function type, so it follows the declarator.
int allocateOnRealtimePath() [[clang::nonblocking]]
{
    std::vector<int> values(1024);
    return static_cast<int>(values.size());
}

} // namespace

int main()
{
    std::printf("allocated %d values on a real-time path\n", allocateOnRealtimePath());
    return 0;
}
