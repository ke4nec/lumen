# Exercise the real windowed loop and require a successful presentation and
# clean exit. The dummy video driver forces probe failure on every platform.
set(frames 1)
if(FAILURE_LIBRARY)
    set(frames 2)
endif()
set(smoke_command "${COUNTER}" --renderer gpu --diagnostics --frames ${frames})
if(FAILURE_LIBRARY)
    set(smoke_command "${CMAKE_COMMAND}" -E env "LD_PRELOAD=${FAILURE_LIBRARY}"
        ${smoke_command})
elseif(EXPECTED_BACKEND STREQUAL "cpu")
    set(smoke_command "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy
        ${smoke_command})
endif()
execute_process(COMMAND ${smoke_command}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors
    TIMEOUT 30)
if(FAILURE_LIBRARY AND NOT output MATCHES
   "gpu failed \\(gl-swap-failed: injected persistent GL swap failure\\)")
    message(FATAL_ERROR "Runtime GL failure was not exercised:\n${output}\n${errors}")
endif()
if(EXPECT_PRESENT_FAILURE)
    if(NOT result STREQUAL "1" OR
       NOT errors MATCHES "injected software present failure" OR
       NOT errors MATCHES "CPU present failed" OR
       output MATCHES "frames=[1-9][0-9]* ")
        message(FATAL_ERROR "Software failure was not reported (${result}):\n${output}\n${errors}")
    endif()
    message(STATUS "${output}\n${errors}")
    return()
endif()
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Counter failed (${result}):\n${output}\n${errors}")
endif()
if(NOT output MATCHES "backend=${EXPECTED_BACKEND} " OR
   NOT output MATCHES "frames=${frames} " OR
   NOT output MATCHES "cmds=[1-9][0-9]* ")
    message(FATAL_ERROR "Expected a frame from ${EXPECTED_BACKEND}:\n${output}\n${errors}")
endif()
if(EXPECTED_BACKEND STREQUAL "cpu" AND NOT FAILURE_LIBRARY AND
   NOT output MATCHES "gpu probe failed")
    message(FATAL_ERROR "GPU probe failure was not exercised:\n${output}\n${errors}")
endif()
if(EXPECTED_BACKEND STREQUAL "skia-gpu" AND
   output MATCHES "falling back to cpu|backend=cpu ")
    message(FATAL_ERROR "GPU presentation fell back to CPU:\n${output}\n${errors}")
endif()
message(STATUS "${output}")
