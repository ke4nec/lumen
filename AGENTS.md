# Repository Guidelines

## Project Structure & Module Organization

Lumen is a C++20, Flutter-inspired, self-drawn UI framework. See [`docs/lumen-gui-framework-plan.md`](docs/lumen-gui-framework-plan.md) for the design baseline.

Current product scope is Windows, Linux, and macOS desktop UI. Android/iOS and milestone M9 are deferred with no scheduled release. Retained mobile seams, font code, build options, and CI jobs are historical experiments, not authorization to extend mobile functionality. Desktop touch input, density settings, and window metrics remain in scope. See [`docs/lumen-self-use-roadmap.md`](docs/lumen-self-use-roadmap.md) for current planning.

- `include/lumen/`: public headers grouped by `core`, `style`, `layout`, `render`, `platform`, and `dsl`.
- `src/`: private implementations matching the public module groups.
- `tests/`: Catch2 unit and headless integration tests.
- `examples/counter/`: the executable interaction sample.
- `docs/`: architecture, DSL, and contributor documentation.
- `cmake/`: dependency and build helpers.

Keep platform code behind interfaces. SDL3 owns windows and input; `CpuRenderer` and optional `SkiaRenderer` consume the same renderer contract.

## Build, Test, and Development Commands

After the CMake skeleton is added, use an out-of-source build:

```sh
cmake -S . -B build -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Run the counter sample from `build/examples/counter/` (Windows adds the `Debug/` configuration directory). Enable Skia with `-DLUMEN_ENABLE_SKIA=ON` when dependencies are available.

## Coding Style & Naming Conventions

Use C++20, four-space indentation, and braces on the declaration line. Use `PascalCase` for types, `camelCase` for functions and variables, and `snake_case` for filenames. Prefer RAII and value types over raw ownership. Keep UI-thread ownership explicit and avoid platform types in `lumen-core` headers.

## Testing Guidelines

Write Catch2 tests beside the subsystem they protect. Name files `*_tests.cpp` and test cases by behavior, such as `row_distributes_flex_space`. Cover layout constraints, hit testing, event bubbling, state invalidation, DSL diagnostics, and CPU framebuffer output. Add a headless test before relying on a desktop smoke test; run the full `ctest` command before submitting changes.

## Commit & Pull Request Guidelines

Use Conventional Commits with an English type, an optional Chinese scope, and a concise Chinese subject:

```text
<type>(<scope>): <中文描述>
```

Allowed types:

- `feat`: 新增用户或框架能力
- `fix`: 修复错误或回归
- `docs`: 更新文档、注释或示例说明
- `test`: 新增或调整测试
- `refactor`: 不改变行为的代码重构
- `perf`: 性能优化
- `build`: 构建系统或依赖变更
- `ci`: 持续集成配置变更
- `chore`: 其他维护性变更
- `revert`: 回滚既有提交

Examples: `feat(layout): 增加 Column 约束布局`, `fix(dsl): 修复属性缺失时的错误提示`, `docs(api): 补充 Renderer 接口说明`. Keep wording specific and omit scope for repository-wide changes. Keep commits focused and buildable. Pull requests should explain the change, list validation commands and platform coverage, link issues, and include screenshots for visual changes. Call out new FetchContent dependencies and required build options.

## Security & Configuration Tips

Do not commit credentials, generated binaries, build directories, or dependency caches. Pin CMake dependency revisions and keep Skia optional.
