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
