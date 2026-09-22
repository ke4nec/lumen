# Lumen 发布与安装产物

Lumen 有两类产物，职责不混用：

| 产物 | 入口 | 用途 | 内容边界 |
| --- | --- | --- | --- |
| 开发归档 | CPack `TGZ`（Linux）或 `ZIP`（Windows/macOS） | 分发静态库和头文件、示例、文档 | `lib/`、`include/`、`bin/`、`share/`、`lib/cmake/Lumen/` |
| 运行时应用 | Linux AppImage、macOS `Lumen.app.zip` | 双击或命令行运行示例应用 | 运行时可执行文件、动态依赖、桌面资源；不作为 SDK |

`LumenConfig.cmake`、`LumenConfigVersion.cmake` 和
`LumenTargets.cmake` 随开发归档安装。消费者可以使用：

```cmake
find_package(Lumen CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Lumen::lumen-app)
```

桌面 `lumen-platform`/`lumen-app` 静态目标通过导出的 `SDL3::SDL3` 目标链接；
该目标及 SDL3 头文件、运行库随开发归档安装。启用 Skia 或 GPU 时，消费者还应在
自己的工具链中提供对应 Skia/OpenGL 依赖。核心、布局、渲染、
无障碍和 DSL 静态目标都在同一导出集合中，`lumen-render` 不再遗漏。

CI 负责验证每种产物的职责：

- Linux `package`/`package-skia`/`package-skia-gpu` 运行 CPack 解包冒烟；
  `package` 另外用 linuxdeploy 从同一 install tree 组装 Settings 和 Gallery
  AppImage。
- Windows `package` 只发布 CPack ZIP 开发归档，并在解包目录运行 headless 示例。
- macOS `package` 发布 CPack ZIP 开发归档，同时把其中的运行时文件组装为
  `Lumen.app.zip` 并执行 bundle 内 headless 冒烟。

AppImage 和 `.app` 的启动验证不替代真实桌面窗口、输入法、剪贴板、透明合成或
屏幕阅读器验收；这些由 `platform-acceptance.yml` 和平台人工验收流程负责。
