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
