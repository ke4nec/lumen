# Lumen

C++20 自绘 GUI 框架（Flutter 式声明式 UI），详见
[`docs/lumen-gui-framework-plan.md`](docs/lumen-gui-framework-plan.md)。

当前进度：阶段 0（工程骨架）+ 阶段 1（核心树和布局）。

## 结构

- `include/lumen/`：`core`、`layout`、`render`、`platform`、`dsl` 公共头文件。
- `src/`：与公共模块一一对应的实现。
- `tests/`：Catch2 单测（几何、布局、Element/RenderNode）。
- `examples/counter/`：SDL3 空窗口示例（阶段 0 可运行闭环）。
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
./build/examples/counter/lumen-counter
```

Skia 预留选项（阶段 5 实现）：`-DLUMEN_ENABLE_SKIA=ON`，默认 `OFF`。
