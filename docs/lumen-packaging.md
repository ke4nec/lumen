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
# Windows Skia 预编译库使用静态 CRT；须在 add_executable 前采用同一设置。
if(MSVC AND LUMEN_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "${LUMEN_MSVC_RUNTIME_LIBRARY}")
endif()
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Lumen::lumen-app)
```

桌面 `lumen-platform`/`lumen-app` 静态目标通过导出的 `SDL3::SDL3` 目标链接；
该目标及 SDL3 头文件、运行库随开发归档安装。启用 Skia 时，实际链接的 Skia
及附带静态依赖归档安装到 `lib/lumen/skia/`，许可证随文档安装；Windows
额外需要的 zlibstatic 同时导出。消费者无需构建机上的 Skia 缓存。
系统开发依赖仍需提供：Linux 的 D-Bus、Fontconfig、zlib，macOS 的系统框架和
zlib，以及 GPU 包的 OpenGL。消费者须使用兼容的体系结构、工具链和 Release
配置。`LUMEN_WITH_SKIA` / `LUMEN_WITH_GPU` 标明包中编入的后端。核心、布局、渲染、
无障碍和 DSL 静态目标都在同一导出集合中，`lumen-render` 不再遗漏。

CI 负责验证每种产物的职责：

- Linux `package`/`package-skia`/`package-skia-gpu` 运行 CPack 解包冒烟；
  `package` 另外用 linuxdeploy 从同一 install tree 组装 Settings 和 Gallery
  AppImage。
- Windows `package` 只发布 CPack ZIP 开发归档，并在解包目录运行 headless 示例。
- macOS `package` 发布 CPack ZIP 开发归档，同时把其中的运行时文件组装为
  `Lumen.app.zip` 并执行 bundle 内 headless 冒烟。

所有桌面 SDK 打包 job 还运行 `tests/package_consumer_smoke.py`：把解包产物
复制到仓库外的含空格路径，拒绝导出配置泄漏源码/构建/原安装目录，独立
`find_package(Lumen)`、编译、链接并运行消费者。Skia 包实际光栅绘制；GPU 包
同时链接 GPU 工厂并检查无窗口失败契约。此检查不依赖源码树参与构建，也不
将无窗口检查当作 GPU 渲染验收。

AppImage 和 `.app` 的启动验证不替代真实桌面窗口、输入法、剪贴板、透明合成或
屏幕阅读器验收；这些由 `platform-acceptance.yml` 和平台人工验收流程负责。
