# G-2：崩溃注入冒烟（script 模式 CTest；参照 counter_gpu_smoke.cmake
# 的组织方式）。断言：
#   1. segv 注入 → 退出码非 0（真实信号语义），崩溃报告含
#      signal=SIGSEGV、日志尾部 injector-alive、backtrace；脏标记残留。
#   2. query → last-run-crashed=1（脏标记检测），干净退出清标记。
#   3. 再次 query → last-run-crashed=0（正常退出不留脏标记）。
#   4. abort / terminate 注入同口径（信号名分别 SIGABRT / std::terminate）。

if(NOT DEFINED INJECTOR)
    message(FATAL_ERROR "INJECTOR target-file not provided (-DINJECTOR=...)")
endif()

function(run_case out_result out_output dir mode)
    execute_process(
        COMMAND "${INJECTOR}" "${dir}" "${mode}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE errorOutput
        TIMEOUT 30)
    set(${out_result} "${result}" PARENT_SCOPE)
    set(${out_output} "${output}" PARENT_SCOPE)
    if(NOT result EQUAL 0 AND NOT mode MATCHES "segv|abort|terminate")
        message(FATAL_ERROR "injector (${mode}) failed: ${errorOutput}")
    endif()
endfunction()

function(expect condition_message)
    # 调用方传正向条件（如 report MATCHES "..."）；${ARGN} 展开进 if。
    if(NOT ${ARGN})
        message(FATAL_ERROR "expectation failed: ${condition_message}")
    endif()
endfunction()

function(expect_crash_exit mode result)
    # 崩溃注入必须以非 0 退出（真实信号语义，不伪装干净退出）。
    if(result EQUAL 0)
        message(FATAL_ERROR
                "crash injection (${mode}) must exit non-zero, got 0")
    endif()
endfunction()

set(root "${CMAKE_CURRENT_BINARY_DIR}/crash-injector-smoke")

# --- SIGSEGV ---
set(dir "${root}/segv")
file(REMOVE_RECURSE "${dir}")
run_case(result output "${dir}" segv)
expect_crash_exit(segv "${result}")
file(READ "${dir}/inject-crash.txt" report)
expect("segv report contains signal name"
       report MATCHES "signal=SIGSEGV")
expect("segv report embeds log tail" report MATCHES "injector-alive")
expect("segv report contains backtrace section" report MATCHES "backtrace")
expect("dirty marker survives crash" EXISTS "${dir}/inject.running")

run_case(result output "${dir}" query)
expect("query after crash exits clean" result EQUAL 0)
expect("query detects last-run-crashed" output MATCHES "last-run-crashed=1")
run_case(result output "${dir}" query)
expect("clean exit leaves no dirty marker"
       output MATCHES "last-run-crashed=0")

# --- SIGABRT ---
set(dir "${root}/abort")
file(REMOVE_RECURSE "${dir}")
run_case(result output "${dir}" abort)
expect_crash_exit(abort "${result}")
file(READ "${dir}/inject-crash.txt" report)
expect("abort report contains signal name"
       report MATCHES "signal=SIGABRT")
expect("abort report embeds log tail" report MATCHES "injector-alive")
run_case(result output "${dir}" query)
expect("abort detected next run" output MATCHES "last-run-crashed=1")

# --- std::terminate（未捕获异常） ---
set(dir "${root}/terminate")
file(REMOVE_RECURSE "${dir}")
run_case(result output "${dir}" terminate)
expect_crash_exit(terminate "${result}")
file(READ "${dir}/inject-crash.txt" report)
expect("terminate report labeled std::terminate"
       report MATCHES "signal=std::terminate")
expect("terminate report embeds log tail" report MATCHES "injector-alive")
run_case(result output "${dir}" query)
expect("terminate detected next run" output MATCHES "last-run-crashed=1")

message(STATUS "crash injector smoke passed")
