# Version information: project version from CMakeLists.txt plus the git commit, if available.

find_package(Git QUIET)
set(AF_GIT_COMMIT "unknown")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=10 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE AF_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT AF_GIT_COMMIT)
        set(AF_GIT_COMMIT "unknown")
    endif()
endif()
message(STATUS "AudioForge ${PROJECT_VERSION} (${AF_GIT_COMMIT})")
