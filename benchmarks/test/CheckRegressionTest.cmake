# Self-test of check_regression.py: an unchanged report passes, a 2x slowdown fails.
# Inputs: PYTHON, SCRIPT, WORK_DIR.
file(MAKE_DIRECTORY "${WORK_DIR}")

function(write_report path engine_us)
    file(WRITE "${path}" "{\"benchmarks\": [
  {\"run_name\": \"BM_Calibration\", \"run_type\": \"aggregate\", \"aggregate_name\": \"median\",
   \"real_time\": 100.0, \"time_unit\": \"us\"},
  {\"run_name\": \"BM_Engine64Layers/128\", \"run_type\": \"aggregate\", \"aggregate_name\": \"median\",
   \"real_time\": ${engine_us}, \"time_unit\": \"us\"}]}")
endfunction()

write_report("${WORK_DIR}/base.json" 40.0)
write_report("${WORK_DIR}/same.json" 42.0)
write_report("${WORK_DIR}/slow.json" 80.0)

execute_process(COMMAND "${PYTHON}" "${SCRIPT}" "${WORK_DIR}/base.json" "${WORK_DIR}/baseline.json"
                        --write-baseline RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "writing the baseline failed: ${rc}")
endif()
execute_process(COMMAND "${PYTHON}" "${SCRIPT}" "${WORK_DIR}/same.json" "${WORK_DIR}/baseline.json"
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "a 5% change was reported as a regression")
endif()
execute_process(COMMAND "${PYTHON}" "${SCRIPT}" "${WORK_DIR}/slow.json" "${WORK_DIR}/baseline.json"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out)
if(rc EQUAL 0 OR NOT out MATCHES "REGRESSION")
    message(FATAL_ERROR "a 2x slowdown was not reported:\n${out}")
endif()
