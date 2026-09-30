# Lumen 入门：从模板到你的第一个工具应用

> G-6（gap-backlog）：新应用脚手架与上手路径。本文所有命令可复制执行；
> 模板本体在 [`examples/template/`](../examples/template/)（纳入主构建，
> CI 冒烟防漂移）。

## 0. 前置

按 [README](../README.md#构建) 完成一次全量构建（模板随 `LUMEN_BUILD_EXAMPLES=ON`
自动编译）：

```sh
cmake -S . -B build-debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build-debug -j8
```

跑一次模板 headless 冒烟（应输出确定性帧哈希与命令路径回显）：

```sh
./build-debug/examples/template/lumen-template --headless
# frame0 67c78174332b928d
# counter 2
# dialog 1
```

## 1. 拷出模板

```sh
cp -r examples/template examples/mytool
# 在 CMakeLists.txt 的 examples 区块加一行：
#   add_subdirectory(examples/mytool)
# 全局把 examples/template 改成 examples/mytool（含 CMake 目标名）。
cmake --build build-debug -j8 && ./build-debug/examples/mytool/mytool
```

得到：自定义标题栏窗口（拖拽/双击最大化）、文件/视图菜单（快捷键真实
触发）、侧栏开关、计数器——全部状态关机后保留。

## 2. 模板里有什么（对照 `template_app.h`）

| 能力 | 用的框架设施 | 模板位置 |
| --- | --- | --- |
| 窗口/主循环 | `Sdl3ApplicationHost` + `runApp`（`app_shell.h`） | `main.cpp` `runWindowed` |
| 自定义标题栏 | `WindowDesc.customTitleBar` + `withWindowDrag` + 窗口控制按钮（[titlebar-design](lumen-titlebar-design.md) §5） | `buildTitleBar` |
| 菜单 + 快捷键 | `MenuBarController` + `AppShell::commands()`（G-1 命令注册表，[command-dispatch-design](lumen-command-dispatch-design.md)）——菜单 `.command` 派生快捷键串 | `attach`/`registerCommands` |
| 状态 | `StateStore`（bind 绑定，重建自动同步） | `bump`/`toggleSidebar` |
| 持久化 | `core::Preferences`（G-7，原子写/损坏降级；[preferences-design](lumen-preferences-design.md)）——变更即保存 | `loadState`/`persist` |
| 确认对话框 | `widgets::DialogHost`（G-4a；[dialog-host-design](lumen-dialog-host-design.md)）四钩子接线 | `requestReset` + `configFor` |
| 崩溃兜底/日志 | `RunOptions.diagnosticsDirectory`（G-2；[runtime-diagnostics-design](lumen-runtime-diagnostics-design.md)） | `main.cpp` |
| headless 冒烟 | `renderFrame()` 帧哈希 + `invokeCommand` 路径断言 | `runHeadless` |

## 3. 常见改动

- **加一个菜单项/快捷键**：`registerCommands()` 注册 `CommandSpec`
  （id + `KeyBinding::chord(...)` + 动作），`attach()` 的菜单 provider 里
  加 `{.id = ..., .label = ..., .command = ...}`——快捷键串自动显示，
  无需手写。
- **加一块业务状态**：`StateStore` set/get + `persist()` 落一个键；
  UI 里 `core::makeText(...)`/`bind(...)` 读取。
- **改默认目录/应用名**：`defaultDiagnosticsDirectory("your-app")`
  （Linux `~/.local/share/your-app`，macOS `~/Library/Logs/your-app`）。

## 4. 何时不用模板

- 纯回归冒烟：参考 `examples/counter/`（更小）。
- 复杂控件/主题/无障碍范式：参考 `examples/settings/`、`examples/gallery/`
  （模板刻意不搬它们的演示逻辑）。
