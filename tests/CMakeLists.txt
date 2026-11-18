include(Catch)

add_executable(af_unit_tests
    unit/core/LogTests.cpp
    unit/core/PluralRulesTests.cpp
    unit/core/ResultTests.cpp
    unit/core/TimeTicksTests.cpp
    unit/core/UuidTests.cpp
    unit/core/VersionTests.cpp)
target_link_libraries(af_unit_tests PRIVATE AudioForge::core Catch2::Catch2WithMain)
af_set_warnings(af_unit_tests)
set_target_properties(af_unit_tests PROPERTIES FOLDER "Tests")
catch_discover_tests(af_unit_tests DISCOVERY_MODE PRE_TEST)

# Architecture rules: forbidden #include directions between modules.
add_test(NAME architecture.module_includes
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src
            -P "${CMAKE_CURRENT_SOURCE_DIR}/architecture/CheckIncludes.cmake")

# Smoke tests of the command-line renderer.
add_test(NAME apps.af_render_version COMMAND af-render --version)
set_tests_properties(apps.af_render_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^af-render ${PROJECT_VERSION} ")
add_test(NAME apps.af_render_rejects_unknown_arguments COMMAND af-render --no-such-option)
set_tests_properties(apps.af_render_rejects_unknown_arguments PROPERTIES WILL_FAIL TRUE)

# Start-up/shut-down and crash reporting of the GUI application. On Linux they need a display
# (xvfb-run). AUDIOFORGE_DATA_DIR keeps logs and dumps out of the user's profile.
set(af_gui_test_launcher "")
set(af_can_run_gui ${WIN32})
if(NOT WIN32)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        set(af_gui_test_launcher "${XVFB_RUN}")
        set(af_can_run_gui TRUE)
    endif()
endif()

if(af_can_run_gui)
    if(af_gui_test_launcher)
        add_test(NAME apps.audioforge_starts_and_quits
            COMMAND "${af_gui_test_launcher}" -a $<TARGET_FILE:AudioForge> --smoke-test)
    else()
        add_test(NAME apps.audioforge_starts_and_quits COMMAND AudioForge --smoke-test)
    endif()
    set_tests_properties(apps.audioforge_starts_and_quits PROPERTIES
        TIMEOUT 60
        ENVIRONMENT "AUDIOFORGE_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke-test-data"
        FAIL_REGULAR_EXPRESSION "Leaked objects detected|JUCE Assertion failure")

    add_test(NAME apps.audioforge_crash_is_recorded
        COMMAND "${CMAKE_COMMAND}"
            -DAPP=$<TARGET_FILE:AudioForge>
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/crash-test-data
            -DLAUNCHER=${af_gui_test_launcher}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/apps/CrashTest.cmake")
    set_tests_properties(apps.audioforge_crash_is_recorded PROPERTIES TIMEOUT 90)
endif()

# RealtimeSanitizer canary: must abort when the sanitizer works.
if(AF_ENABLE_RTSAN)
    add_executable(af_rtsan_canary rtsan/RtsanCanary.cpp)
    target_compile_options(af_rtsan_canary PRIVATE -Wno-function-effects)
    set_target_properties(af_rtsan_canary PROPERTIES FOLDER "Tests")
    add_test(NAME rtsan.canary_detects_allocation COMMAND af_rtsan_canary)
    set_tests_properties(rtsan.canary_detects_allocation PROPERTIES WILL_FAIL TRUE)
endif()
