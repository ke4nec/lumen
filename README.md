# Lumen

C++20 自绘 GUI 框架（Flutter 式声明式 UI），详见
[`docs/lumen-gui-framework-plan.md`](docs/lumen-gui-framework-plan.md)。

当前进度：阶段 0（工程骨架）+ 阶段 1（核心树和布局）+ 阶段 2（CPU 渲染与 SDL3 平台层）+ 阶段 3（交互与 C++ DSL）。

## 结构

- `include/lumen/`：`core`、`layout`、`render`、`platform`、`dsl` 公共头文件。
- `src/`：与公共模块一一对应的实现（`render` 含 CPU 光栅器与 painter，`platform` 含 SDL3 后端）。
- `tests/`：Catch2 单测与无窗口集成测试（几何、布局、Element、渲染像素、交互、counter frame hash）。
- `examples/counter/`：可交互 counter 示例（窗口模式 + `--headless`）。
- `cmake/`：FetchContent 依赖声明（SDL3、Catch2，均已 pin 版本）。
- `docs/`：架构与分阶段计划。

## 构建

```sh
cmake -S . -B build -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Windows 可用 VS 自带的 CMake/Ninja，例如：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
```

示例运行（`build/examples/counter/`，Windows 多一层 `Debug/`）：

```sh
# 窗口模式：Button 点击计数、TextField 输入、窗口缩放自适应
./build/examples/counter/lumen-counter
# 无窗口模式：打印稳定 frame hash（点击/输入/缩放各一帧）
./build/examples/counter/lumen-counter --headless
```

Skia 预留选项（阶段 5 实现）：`-DLUMEN_ENABLE_SKIA=ON`，默认 `OFF`。
