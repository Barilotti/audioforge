# RealtimeSanitizer (Clang 20+) detects allocations, locks and blocking calls inside functions
# marked [[clang::nonblocking]]. Used by the RTSan CI job from stage S1 on.

if(AF_ENABLE_RTSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
        message(FATAL_ERROR "AF_ENABLE_RTSAN requires Clang 20 or newer")
    endif()
    add_compile_options(-fsanitize=realtime)
    add_link_options(-fsanitize=realtime)
endif()

# ThreadSanitizer checks the lock-free primitives and the thread hand-over code for data races.
if(AF_ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "AF_ENABLE_TSAN is not supported with MSVC")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
