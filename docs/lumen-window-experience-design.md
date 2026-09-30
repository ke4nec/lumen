# Lumen 窗口体验杂项设计（G-8）

> 状态：部分实现（2026-09-30；gap-backlog G-8，P3）——位置记忆 + 单实例已交付；任务栏进度留三平台原生 seam 按需池。
> 动机：长期驻留工具每天都感知的细节：窗口位置/尺寸记忆、二次启动聚焦已有窗口、任务栏进度。
> 代码：`WindowDesc.x/y` + `WindowMetrics.x/y/positioned`（[`application_host.h`](../include/lumen/platform/application_host.h)/[`windowing.h`](../include/lumen/core/windowing.h)）；SDL/Fake host 实现；`core::SingleInstanceGuard`（[`single_instance.h`](../include/lumen/core/single_instance.h)，POSIX unix socket）；模板接线示范（`examples/template/main.cpp`）。
> 测试：`tests/window_experience_tests.cpp`。

## 1. 窗口位置记忆

- **契约**：`WindowDesc.x/y`（`std::optional<int>`，物理像素；nullopt = 系统默认）；`WindowMetrics.x/y/positioned`（回读快照；`positioned=false` = 平台未提供——应用不做记忆回放）。
- **平台**：SDL host `SDL_SetWindowPosition`/`SDL_GetWindowPosition`（三桌面；能力位 `PlatformCapabilities.windowPosition = true`）；Fake host 确定性回读（headless 契约锁定）。
- **应用模式**（模板示范）：启动 `Preferences.load` → `windowDesc.x/y` 回放；退出 `windowMetrics` → `Preferences.save`。记忆数据属 G-7 助手职责，框架不内置自动记忆（窗口位置是应用数据的一部分）。

## 2. 单实例激活

- **契约**：`core::SingleInstanceGuard::acquire(Config)` 进程一次调用：
  - `Primary`：绑定成功并起守护 accept 线程（`onActivateRequest` 在后台线程触发——应用自行投递 UI 线程，模板注释示范）。
  - `SecondaryActivated`：已有实例且激活请求（`"activate\n"`）送达——应用即刻退出。
  - `SecondaryNotifyFailed`/`Unavailable`：结构化降级，**允许应用继续运行**（不因助手失败丢窗口）。
- **实现**：unix domain socket（`XDG_RUNTIME_DIR|TMPDIR|/tmp` + `lumen-<app>.single-instance`）；先 connect（判 Secondary）后 bind；崩溃残留 socket 文件由 `unlink` 兜底恢复（测试锁定）。socket 目录可注入（headless 确定性/并行隔离）。
- **边界**：纯本地 IPC——unix socket 无网络栈参与，不构成网络能力承诺（路线图 §1.2）；并发 bind 竞态（双 Primary）与 Windows 命名锁 seam 为已知限制（薄助手定位，按需增强）。

## 3. 任务栏进度（未实施，按需池）

SDL 3.2.10 无进度 API（`SDL_SetWindowProgressState/Value` 属 SDL 3.4+）；按 backlog 建议需走三平台原生 seam（Win32 ITaskbarList3 / Unity D-Bus com.canonical.Unity.LauncherEntry / NSDockTile）——超出演示成本，登记按需池：能力位预留 `PlatformCapabilities`（实施时新增 `taskbarProgress`），服务形态参照 `ServiceResult` + 原生 `native_services_*` 模式。

## 4. 验证

- headless（本提交）：Desc 位置应用 + Metrics 回读（带/不带位置两态）、单实例 Primary/Secondary/激活回调异步到达、残留 socket 恢复、Preferences 位置记忆往返。
- 桌面 smoke（人工）：拖动窗口后重启位置保持；二次启动首窗口聚焦（模板直接可验；单实例守卫仅接窗口路径——headless 冒烟不走单实例，并行 CI 下第二实例不会被静默退出）；Wayland 下位置语义由合成器决定（`positioned` 表示"SDL 视角可用"，跨显示器不复位属合成器行为）。
