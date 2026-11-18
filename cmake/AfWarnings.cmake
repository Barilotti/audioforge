# Strict warnings for AudioForge's own targets (never applied to third-party code).

function(af_set_warnings target)
    if(MSVC)
        set(flags /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /w14242 /w14254 /w14263
                  /w14265 /w14287 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14640
                  /w14826 /w14905 /w14906 /w14928
                  # C4324 "structure was padded due to alignment specifier": the padding is the
                  # point of alignas(cacheLineSize) in the real-time primitives.
                  /wd4324)
        if(AF_WARNINGS_AS_ERRORS)
            list(APPEND flags /WX)
        endif()
    else()
        set(flags -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
                  -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wformat=2
                  -Wimplicit-fallthrough -Wmisleading-indentation)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(APPEND flags -Wduplicated-cond -Wduplicated-branches -Wlogical-op)
        endif()
        if(AF_WARNINGS_AS_ERRORS)
            list(APPEND flags -Werror)
        endif()
    endif()
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${flags}>)
endfunction()
