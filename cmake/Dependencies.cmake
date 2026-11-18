# Third-party dependencies. Every version is pinned; update them deliberately in a dedicated PR.
#
# Set CPM_SOURCE_CACHE (environment or cache variable) to share downloads between build trees.

include(CPM)

# JUCE: UI, audio devices, audio formats, DSP. 8.0.17 contains hardening of WAV/AIFF parsing.
CPMAddPackage(
    NAME JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.17-security
    GIT_SHALLOW TRUE)

# xxHash: content hashes of assets (peak cache key, fingerprint for Relink). Header-only use.
CPMAddPackage(
    NAME xxHash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.3
    GIT_SHALLOW TRUE
    DOWNLOAD_ONLY YES)
add_library(af_xxhash INTERFACE)
add_library(AudioForge::xxhash ALIAS af_xxhash)
target_include_directories(af_xxhash SYSTEM INTERFACE "${xxHash_SOURCE_DIR}")

if(AF_BUILD_TESTS)
    CPMAddPackage(
        NAME Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.1
        GIT_SHALLOW TRUE)
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()

if(AF_BUILD_BENCHMARKS)
    CPMAddPackage(
        NAME benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
        GIT_SHALLOW TRUE
        OPTIONS "BENCHMARK_ENABLE_TESTING OFF" "BENCHMARK_ENABLE_INSTALL OFF"
                "BENCHMARK_ENABLE_GTEST_TESTS OFF" "BENCHMARK_ENABLE_WERROR OFF")
endif()

# All JUCE modules are compiled exactly once, into af_juce. AudioForge targets link it
# instead of the juce:: interface targets, which would otherwise compile JUCE sources into
# every target that uses them.
add_library(af_juce STATIC)
add_library(AudioForge::juce ALIAS af_juce)

target_link_libraries(af_juce
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

target_compile_definitions(af_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
        # ASIO stays off until the Steinberg ASIO SDK licence is settled (ADR-0001, section 10).
        JUCE_ASIO=0
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0
        JUCE_JACK=0
        # Linux builds are for CI and development only and have no sound hardware: ALSA would
        # only trip JUCE assertions when the MIDI sequencer is missing. Tests use fake devices.
        $<$<PLATFORM_ID:Linux>:JUCE_ALSA=0>
        # MP3 decoding arrives in stage S10.
        JUCE_USE_MP3AUDIOFORMAT=0
        JUCE_MODAL_LOOPS_PERMITTED=0
    INTERFACE
        $<TARGET_PROPERTY:af_juce,COMPILE_DEFINITIONS>)

# SYSTEM: our strict warnings must not fire inside JUCE headers.
target_include_directories(af_juce SYSTEM
    INTERFACE
        $<TARGET_PROPERTY:af_juce,INCLUDE_DIRECTORIES>)

set_target_properties(af_juce PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VISIBILITY_INLINES_HIDDEN ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    FOLDER "ThirdParty")
