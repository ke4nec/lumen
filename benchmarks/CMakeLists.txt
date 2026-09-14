# v0.2 阶段7A: 独立基准目标，默认不随常规构建打开
# (LUMEN_BUILD_BENCHMARKS=ON)。Headless CPU-only，不依赖 SDL/窗口。
# M0 基线冻结：把构建类型编译进报告，保证基线 JSON 携带 build_type；
# toolchain/commit/platform 允许运行时经环境变量覆盖（见 scene_bench.cpp）。
add_executable(lumen-scene-bench scene_bench.cpp)

target_include_directories(lumen-scene-bench PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(lumen-scene-bench PRIVATE
    lumen-core
    lumen-layout
    lumen-render
    lumen-accessibility
)

# M7：--backend skia（离屏光栅 SkiaRenderer；同场景同工具链比较）。
if(LUMEN_ENABLE_SKIA)
    target_compile_definitions(lumen-scene-bench PRIVATE LUMEN_BENCH_HAS_SKIA=1)
endif()

target_compile_features(lumen-scene-bench PRIVATE cxx_std_20)

# The benchmark deliberately routes global new/delete through malloc/free so
# allocation traffic can be counted. GCC's mismatched-new-delete diagnostic
# does not account for this paired implementation.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(lumen-scene-bench PRIVATE -Wno-mismatched-new-delete)
endif()

# Use the generator's active configuration so multi-config generators
# (Visual Studio, Xcode, Ninja Multi-Config) report the configuration selected
# by `cmake --build --config`, rather than the usually-empty CMAKE_BUILD_TYPE.
target_compile_definitions(lumen-scene-bench PRIVATE
    LUMEN_BENCH_BUILD_TYPE=\"$<CONFIG>\")

if(WIN32)
  # 基准在无控制台的 GUI 子系统环境下也要能输出报告。
  set_target_properties(lumen-scene-bench PROPERTIES
      WIN32_EXECUTABLE OFF
      CONSOLE_EXECUTABLE ON
  )
endif()
