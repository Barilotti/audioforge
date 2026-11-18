# Runs AudioForge --crash-test with a temporary data folder and checks that the crash is
# recorded: the process must fail, and on Windows a minidump must appear in Crashes/.
# Usage: cmake -DAPP=<AudioForge executable> -DWORK_DIR=<temp dir> [-DLAUNCHER=xvfb-run] -P CrashTest.cmake

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(ENV{AUDIOFORGE_DATA_DIR} "${WORK_DIR}")

set(command "${APP}" --crash-test)
if(LAUNCHER)
    set(command "${LAUNCHER}" -a ${command})
endif()

execute_process(COMMAND ${command} RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET TIMEOUT 60)
if(result EQUAL 0)
    message(FATAL_ERROR "AudioForge --crash-test exited normally; expected a crash")
endif()

file(GLOB logs "${WORK_DIR}/Logs/*.log")
if(NOT logs)
    message(FATAL_ERROR "No log file was written before the crash")
endif()
file(READ "${logs}" log_text)
if(NOT log_text MATCHES "Crash test requested")
    message(FATAL_ERROR "The log does not record the crash test")
endif()

if(WIN32)
    file(GLOB dumps "${WORK_DIR}/Crashes/AudioForge_*.dmp")
    if(NOT dumps)
        message(FATAL_ERROR "No minidump was written to ${WORK_DIR}/Crashes")
    endif()
    file(SIZE "${dumps}" dump_size)
    if(dump_size LESS 1024)
        message(FATAL_ERROR "Minidump ${dumps} is suspiciously small (${dump_size} bytes)")
    endif()
    message(STATUS "Minidump written: ${dumps} (${dump_size} bytes)")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
