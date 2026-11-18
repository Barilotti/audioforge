include(Catch)

add_executable(af_unit_tests
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

# RealtimeSanitizer canary: must abort when the sanitizer works.
if(AF_ENABLE_RTSAN)
    add_executable(af_rtsan_canary rtsan/RtsanCanary.cpp)
    target_compile_options(af_rtsan_canary PRIVATE -Wno-function-effects)
    set_target_properties(af_rtsan_canary PROPERTIES FOLDER "Tests")
    add_test(NAME rtsan.canary_detects_allocation COMMAND af_rtsan_canary)
    set_tests_properties(rtsan.canary_detects_allocation PROPERTIES WILL_FAIL TRUE)
endif()
