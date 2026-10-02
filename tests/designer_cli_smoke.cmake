if(NOT DEFINED DESIGNER)
  message(FATAL_ERROR "DESIGNER executable is required")
endif()

execute_process(
    COMMAND "${DESIGNER}" --max-frames invalid
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR "invalid --max-frames was accepted")
endif()
if(NOT error MATCHES "usage error: --max-frames expects an unsigned integer")
  message(FATAL_ERROR "unexpected diagnostic: ${error}")
endif()

execute_process(
    COMMAND "${DESIGNER}" --unknown-option
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR "unknown option was accepted")
endif()
if(NOT error MATCHES "usage error: unknown option: --unknown-option")
  message(FATAL_ERROR "unexpected unknown-option diagnostic: ${error}")
endif()

execute_process(
    COMMAND "${DESIGNER}" --file
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR "missing --file value was accepted")
endif()
if(NOT error MATCHES "usage error: --file requires a value")
  message(FATAL_ERROR "unexpected missing-file diagnostic: ${error}")
endif()

execute_process(
    COMMAND "${DESIGNER}" --file --max-frames 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR "an option was accepted as the --file value")
endif()
if(NOT error MATCHES "usage error: --file requires a value")
  message(FATAL_ERROR "unexpected option-as-file diagnostic: ${error}")
endif()

set(missing_file "${CMAKE_CURRENT_BINARY_DIR}/designer-cli-missing.design")
file(REMOVE "${missing_file}")
execute_process(
    COMMAND "${DESIGNER}" --headless --file "${missing_file}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR "missing --file was accepted")
endif()
if(NOT output MATCHES "diagnostics [1-9][0-9]*")
  message(FATAL_ERROR "missing --file did not report a diagnostic: ${output}")
endif()

execute_process(
    COMMAND "${DESIGNER}" --file "${missing_file}" --max-frames 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR "window mode accepted a missing --file")
endif()
