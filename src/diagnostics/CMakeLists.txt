# G-2（gap-backlog）：崩溃兜底与持久日志。
#
# 平台无关模块（POSIX 信号 + execinfo 探测退化；公共头无平台 SDK 类型，
# 与 M2 接口约束一致）。runApp 经 RunOptions.diagnosticsDirectory 可选接
# 线，默认关闭零开销。
include(CheckIncludeFileCXX)
check_include_file_cxx(execinfo.h LUMEN_HAVE_EXECINFO)

add_library(lumen-diagnostics STATIC
    runtime_diagnostics.cpp
)

target_include_directories(lumen-diagnostics PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(lumen-diagnostics PUBLIC cxx_std_20)
if(LUMEN_HAVE_EXECINFO)
    target_compile_definitions(lumen-diagnostics PRIVATE LUMEN_HAVE_EXECINFO)
endif()

find_package(Threads REQUIRED)
target_link_libraries(lumen-diagnostics PUBLIC Threads::Threads)
