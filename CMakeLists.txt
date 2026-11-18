cmake_minimum_required(VERSION 3.25)

project(AudioForge
    VERSION 0.1.0
    DESCRIPTION "Universal sound design workstation"
    LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset <name>")
endif()

# --- Options -----------------------------------------------------------------
option(AF_BUILD_TESTS "Build unit tests" ON)
option(AF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in AudioForge targets" ON)
option(AF_ENABLE_RTSAN "Build with Clang RealtimeSanitizer (-fsanitize=realtime)" OFF)
option(AF_ENABLE_TSAN "Build with ThreadSanitizer (-fsanitize=thread)" OFF)

# --- Global settings ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    # Static C++ runtime: the installer does not need to ship the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # RelWithDebInfo is the shipped configuration. CMake's default /Ob1 inlines only functions
    # marked inline, which leaves JUCE's renderer and our DSP templates several times slower
    # than in Release; /Ob2 inlines like Release and keeps the debug information.
    foreach(lang C CXX)
        string(REPLACE "/Ob1" "/Ob2" CMAKE_${lang}_FLAGS_RELWITHDEBINFO
                       "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}")
    endforeach()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(AfWarnings)
include(AfSanitizers)
include(AfVersion)
include(AfModule)
include(Dependencies)

# --- Code --------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(apps)

if(AF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
