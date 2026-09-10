# v0.2 阶段7A: 独立基准目标，默认不随常规构建打开
# (LUMEN_BUILD_BENCHMARKS=ON)。Headless CPU-only，不依赖 SDL/窗口。
add_executable(lumen-scene-bench scene_bench.cpp)

target_include_directories(lumen-scene-bench PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(lumen-scene-bench PRIVATE
    lumen-core
    lumen-layout
    lumen-render
)

target_compile_features(lumen-scene-bench PRIVATE cxx_std_20)

if(WIN32)
  # 基准在无控制台的 GUI 子系统环境下也要能输出报告。
  set_target_properties(lumen-scene-bench PROPERTIES
      WIN32_EXECUTABLE OFF
      CONSOLE_EXECUTABLE ON
  )
endif()
