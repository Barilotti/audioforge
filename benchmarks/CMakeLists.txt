# Engine benchmarks (architecture v0.2, section 7.3; ADR-0017). Built with AF_BUILD_BENCHMARKS,
# compared against benchmarks/baseline/ by check_regression.py in CI.
add_executable(af_benchmarks
    BenchmarkMain.cpp
    EngineBenchmarks.cpp)
target_link_libraries(af_benchmarks PRIVATE AudioForge::core AudioForge::model AudioForge::assets
                                            AudioForge::engine benchmark::benchmark)
# Reuses the engine test fixture (projects with generated audio).
target_include_directories(af_benchmarks PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit/engine")
af_set_warnings(af_benchmarks)
set_target_properties(af_benchmarks PROPERTIES FOLDER "Benchmarks")

if(AF_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME benchmarks.check_regression_detects_slowdown
            COMMAND "${CMAKE_COMMAND}" -DPYTHON=${Python3_EXECUTABLE}
                    -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/check_regression.py
                    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/regression-self-test
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/test/CheckRegressionTest.cmake")
    endif()
endif()
