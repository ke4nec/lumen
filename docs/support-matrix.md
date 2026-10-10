# Lumen 桌面平台支持矩阵

> 状态：2026-10-02 核对；保留 M0 四态定义，当前能力覆盖 M0–M8、M10–M18 实现批次（M15 拖放、M16 窗口与系统集成、M17 控件细节池、M18 开发者诊断首批），各批次四态见下方「实现批次与真实平台缺口」；M9 冻结，不计入当前完成范围。三平台屏幕阅读器回环仍需真实桌面验收。
> 后续完善顺序、缺口编号（R0–R11）与验收模板以 [`lumen-gui-completion-plan.md`](lumen-gui-completion-plan.md) 为任务入口；待现场验收项逐条登记于 [`platform-acceptance.md`](platform-acceptance.md)。
> 当前产品范围（2026-09-14）：Windows/Linux/macOS 桌面；Android/iOS 暂不实现并冻结，M9 仅保留历史编号。
> 构建命令与系统依赖的单一事实来源是
> [`build-commands.md`](build-commands.md) 与 `.github/workflows/`。
>
> 面向自用工具类应用的后续里程碑见
> [`lumen-self-use-roadmap.md`](lumen-self-use-roadmap.md)。
> 性能基线见 [`perf-baselines/README.md`](perf-baselines/README.md)。

## 能力四态定义（M0 冻结）

后续里程碑必须用以下四种状态描述能力，不得把“接口存在”标记为“平台完成”：

| 状态 | 含义 | 证据要求 |
| --- | --- | --- |
| 接口已存在 | 公共头文件冻结，Fake/Recording 可断言 | 头文件 + headless fake 测试 |
| headless 已验证 | 确定性单测/headless 集成通过 | `ctest` 全量通过（含 Fake host/Recording bridge） |
| 真实平台已验证 | 在目标 OS/驱动/输入法上 smoke 通过 | 窗口 smoke、真实输入法/剪贴板/GPU 呈现记录 |
| 可发布 | 便携包可在干净机器运行并可追溯 | install 产物 + 解包启动 + 版本/commit 追溯 |

## 工具链与依赖基线（M0 冻结，三桌面 CI）

| 平台 | runner | 编译器 | 系统依赖安装方式 |
| --- | --- | --- | --- |
| Windows | `windows-2025` | MSVC（runner 自带工具链，具体版本以构建日志为准；`/utf-8 /W4`，Skia 时强制静态 CRT/MT，Release） | 无额外系统包；Skia 预编译自动拉取 |
| Linux | `ubuntu-24.04` | GCC（系统默认，`-Wall -Wextra -Wpedantic`；Skia Release 经 clang-12 官方构建包链接） | `LUMEN_LINUX_DEPS`（见 `linux.yml` env，与 `build-commands.md` 同源）+ GPU job 追加 `libgl1-mesa-dri mesa-utils` |
| macOS | `macos-15` | AppleClang（Xcode CLT） | 无额外系统包；SDL3 经 FetchContent 编译 |

固定第三方版本（`cmake/dependencies.cmake`）：SDL3 `release-3.2.10`、
Catch2 `v3.8.1`、stb `2c980bb59875b0d32144a71867fbdebb2f77cd20`、
Skia `m124-08a5439a6b`（Windows/Linux/macOS 预编译 Release 包；macOS 按 arm64/x64 选包）、
zlib `v1.3.1`（仅 Windows Skia）。新增 FetchContent 依赖时固定版本、
记录许可证，并说明 CPU-only 行为。

## 桌面平台

| 平台 | 窗口/输入 | 剪贴板 | 文本/IME | 无障碍 | 渲染 | CI 验证 |
| --- | --- | --- | --- | --- | --- | --- |
| Windows | SDL3（宿主多窗口、resize/DPI、触摸 pointer id；`runApp` 按 `WindowId` 隔离多个 `AppShell`） | SDL3 剪贴板（`platform::Clipboard`） | UTF-8 commit + IME preedit（TSF 经 SDL）；修饰键/逻辑键归一化 | 语义树 + Recording 桥；UIA 已实施，需 `LUMEN_ENABLE_ACCESSIBILITY_BRIDGE=ON`；未编入时能力报告 false | CPU、Skia 光栅、Skia GPU（软件回退见下表） | `windows.yml`：cpu / a11y-bridge / skia-raster / skia-gpu / package / package-skia / package-skia-gpu；platform-acceptance 的登录 Win32 runner 负责透明合成、字体冷启动、触摸板和 Narrator/NVDA |
| Linux | SDL3（X11/Wayland） | 同上 | UTF-8 + IBus/Fcitx preedit（候选词锚点经 `SDL_SetTextInputArea`） | 语义树 + Recording 桥；AT-SPI2 provider 已编入（需桌面总线），Orca 回环待验 | 同上 | `linux.yml`：cpu / skia / skia-gpu / package / package-skia / package-skia-gpu；`.github/workflows/platform-acceptance.yml` 的 self-hosted X11/Wayland job 负责真实窗口、IME、剪贴板、GPU/present 故障注入 |
| macOS | SDL3（菜单关闭经统一关闭规则） | 同上 | UTF-8 + 输入法 preedit（经 SDL） | 语义树 + Recording 桥；NSAccessibility provider 已编入（需 NSWindow），VoiceOver 回环待验 | 同上 | `macos.yml`：cpu / skia-gpu / package / package-skia / package-skia-gpu；platform-acceptance 的登录 Aqua runner 负责真实 IME、生命周期、透明窗口和 VoiceOver |

三平台共用：`ApplicationHost` 契约、归一化 `HostEvent`（时间戳/修饰键/
逻辑与物理键/指针设备/pointer id/滚轮/取消/关闭请求）、语义树与 action
分发、`lumen-text` 编辑模型。counter/settings 示例三平台同源。SDL 宿主在
初始化及运行期间约每秒查询 `PlatformCapabilities.highContrast`、
`reduceAnimation`、`fontScale`：Linux 使用异步 portal D-Bus settings，macOS 使用
AppKit display preferences/preferred body font，Windows 使用 SystemParametersInfo
与 Accessibility 注册表；查询不可用时保留默认值或上次有效快照。变化经
`SystemAccessibilityChanged` 广播，`runApp` 首帧前及事件到达时自动应用到全部
跟随窗口，应用逐项显式覆盖优先，详见 [视觉系统 §4.2](lumen-visual-system-design.md#42-系统可访问性偏好与应用覆盖)。

Linux 优先读取 [portal 标准键](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html)
`contrast`/`reduced-motion`，再回退到 GNOME 键；字体缩放使用后端暴露的 GNOME
`text-scaling-factor`。未暴露该键的桌面保留 `1.0`，不宣称覆盖所有桌面设置。
使用 `ReadAll` 避免旧 `Read` 的双层 variant 陷阱，解析仍容忍嵌套 variant；
非法/非有限缩放回退 `1.0`。portal 服务重启可重新采样，会话总线断开后须重新初始化
宿主。macOS preferred body font 需 11.0，减少动态效果查询需 10.12，低版本
保留对应默认值；这不是任意应用字体设置的全局缩放接口。

### 验证快照（行内标注日期与源码基线）

| 环境 | 已核对证据 | 验证边界 |
| --- | --- | --- |
| 本地 Linux / GCC（2026-10-08，源码基线 `a1e012e` + DP-8 门禁批次） | `build-debug` 全量 `ctest` 1116/1116、Release 全量 `ctest` 1118/1118 通过；前者未设置 `CMAKE_BUILD_TYPE`，采集器如实报告 `Unspecified`。工程资源与失败恢复回归仍覆盖；新增四类 Designer 阶段/堆采集器、报告完整性和性能门禁测试，Linux CPU CI 已配置固定源码的同机对比 | 本地 headless 回归不产生真实平台证据；性能 CI 尚未运行，本地计时另见 Designer 基线；allocator source、IME、读屏、拖入和合成器仍按 platform-acceptance 保持 pending |
| 本地 Linux / GCC（2026-10-08，`e46d693` + 原生 allocator 批次） | 默认配置全量 CTest 1120/1120；启用 profiler 的 Release 全量 1127/1127，通过五组独立进程原生测试；GNOME/Mutter 50.1 Wayland 和 Xwayland 两窗口短 smoke 通过，183/175 个有效原生 allocator 帧 | 实际 glibc 原生读数已有单项窗口证据，见 `platform-evidence/frame-allocator-linux-2026-10-08.json`；该源码批次未提交时采集且无完整 workflow/人工 record，不替代独立 X11 桌面、Windows/macOS、输入法、读屏、浸泡或真实 CI 的完整验收 |
| 本地 Linux / GCC（2026-10-08，`580ea50`） | 默认配置全量 CTest 1128/1128，启用 Linux profiler 的 Release 全量 1135/1135；菜单键回归修复前复现 Release 段错误，修复后 27 个断言通过。干净提交与 `a1e012e` 的四类 DP-8 对比按既定 minimum/median 联合规则通过；median 的 `l0_12` paint p95 +12.21% 回退保留 | 本地性能证据见 `platform-evidence/designer-perf-linux-2026-10-08.json`；无新的原生 Windows/macOS CI 或完整桌面记录，不能推进“真实平台已验证”或“可发布”状态 |
| GitHub Linux/macOS CI（2026-10-09，`cebe883`） | [Linux](https://github.com/ke4nec/lumen/actions/runs/37875132290) 与 [macOS](https://github.com/ke4nec/lumen/actions/runs/37875132300) 全部 job 通过，覆盖 CPU、Skia/GPU 和三种安装包；Linux CPU 包含真实 DP-8 同机性能门禁，macOS CPU 包含五组原生 allocator 独立进程回归 | 结果仅属于该提交；Windows 同批因测试文件的 `windows.h` min/max 宏污染编译失败，修复后另行验收。原生进程及安装包 CI 不替代登录桌面、IME、读屏、合成器和完整人工 record |
| GitHub 三桌面 CI（2026-10-09，`85314c0`） | [Windows](https://github.com/ke4nec/lumen/actions/runs/37928701758)、[Linux](https://github.com/ke4nec/lumen/actions/runs/37928701730)、[macOS](https://github.com/ke4nec/lumen/actions/runs/37928701716) 全部 job 通过；Windows SDL3 DLL、默认系统字体冷扫描与 MSVC Debug Widget 预算门禁已修复，三平台含 Designer smoke 和原生 allocator 回归 | 属于该提交的常规 CI 证据；没有 self-hosted platform-acceptance 运行、Designer 人工编辑/读屏或完整桌面 record |
| GitHub 三桌面 CI（2026-10-09，`74a568c`） | [Windows](https://github.com/ke4nec/lumen/actions/runs/37937789284)、[Linux](https://github.com/ke4nec/lumen/actions/runs/37937789425)、[macOS](https://github.com/ke4nec/lumen/actions/runs/37937789233) 全部 job 通过，Windows 七个 job 全通过；包含字段守护、冲突恢复和撤销合并边界 | 结果只归属该提交，不覆盖后续加载/编辑发布修复；没有新的完整人工桌面记录 |
| GitHub 三桌面 CI（2026-10-09，`09705d9`） | [Windows](https://github.com/ke4nec/lumen/actions/runs/37941712087)、[Linux](https://github.com/ke4nec/lumen/actions/runs/37941712200)、[macOS](https://github.com/ke4nec/lumen/actions/runs/37941712074) 全部 job 通过，Windows 七个 job 全通过；覆盖加载/编辑发布修复 | 结果只归属该提交，不覆盖后续工程页会话修复；没有新的完整人工桌面记录 |
| GitHub 三桌面 CI（2026-10-09，`a592118`） | [Windows](https://github.com/ke4nec/lumen/actions/runs/37946377217)、[Linux](https://github.com/ke4nec/lumen/actions/runs/37946377159)、[macOS](https://github.com/ke4nec/lumen/actions/runs/37946377270) 全部 job 通过，Windows 七个 job 全通过；覆盖工程页会话修复 | 结果只归属该提交，不覆盖后续诊断修复；没有新的完整人工桌面记录 |
| GitHub 三桌面 CI（2026-10-09，`f930129`） | [Windows](https://github.com/ke4nec/lumen/actions/runs/37950109487)、[Linux](https://github.com/ke4nec/lumen/actions/runs/37950109507)、[macOS](https://github.com/ke4nec/lumen/actions/runs/37950109511) 全部通过，覆盖诊断阶段、来源和 CLI JSON 修复 | 结果只归属该提交；取消的重复运行不计通过，没有新的完整人工桌面记录 |
| GitHub 三桌面 CI（2026-10-09，`f209ed2`） | [Windows](https://github.com/ke4nec/lumen/actions/runs/37955899483)、[Linux](https://github.com/ke4nec/lumen/actions/runs/37955899724)、[macOS](https://github.com/ke4nec/lumen/actions/runs/37955899492) 全部通过，覆盖 DPI/zoom、嵌套布局与手柄坐标修复 | 结果只归属该提交，不覆盖后续图片会话与双窗口上传修复；没有新的完整人工桌面记录 |
| GitHub 三桌面 CI（2026-10-10，`379275f`） | [Windows](https://github.com/ke4nec/lumen/actions/runs/38012556962)、[Linux](https://github.com/ke4nec/lumen/actions/runs/38012556969) 和 [macOS](https://github.com/ke4nec/lumen/actions/runs/38012556957) 全部通过，Windows 七个 job 全通过；覆盖图片会话与双窗口上传修复，Linux 含 Designer 相对性能门禁 | 结果只归属该提交，不覆盖后续 Escape 修复；没有新的完整人工桌面记录 |
| 本地 GNOME/Wayland Designer（2026-10-09，`85314c0` 运行时源码） | 指定原生 Wayland driver，`gallery.design` 双窗口三帧 smoke 退出 0，唯一完整 `designer_window_smoke pass` 行；[原始日志与身份摘要](platform-evidence/designer-wayland-2026-10-09.json) 已保留 | 工作树含文档/验收脚本改动，运行时源码与提交一致；仅窗口启动，没有编辑/保存/重开、Inspector 操作、IME、读屏、高 DPI、GPU 或一小时浸泡验收 |
| 本地 Windows / VS 2026 / CPU | Debug 与 Release 构建成功；全量 CTest 各 779/779，通过；日志在 `build-debug/Testing/Temporary/LastTest.log`、`build-release/Testing/Temporary/LastTest.log` | 本次测试使用默认 OFF 的 GPU、Skia 和原生无障碍开关；包含历史移动接缝回归，不代表移动设备验收；测试代码仍有 MSVC 警告 |
| Windows CI | [windows 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116368) 全部 job 成功，含 UIA 开关 ON + live smoke、Skia/GPU 与打包 | UIA 客户端冒烟不替代讲述人/NVDA；GPU 不可用时用例可跳过，不能仅凭绿色 job 认定实际 GPU 提交 |
| Linux CI | [linux 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116406) 全部 job 成功，含 Xvfb/llvmpipe GPU、CPU/Skia 包与 AppImage 构建 | GPU 窗口 smoke 显式断言 `backend=skia-gpu`；不替代 Wayland、真实输入法/触控板/合成器及干净桌面包验收 |
| macOS CI | [macos 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116325) 全部 job 成功，含 Skia/GPU、CPU `.app` 骨架与 Skia zip | GPU 探测不可用时测试可跳过，窗口命令成功退出本身不排除软件回退；不替代 VoiceOver 与人工窗口验收 |

以上结果仅属于该源码提交，不能作为后续提交自动通过的证据。支持矩阵中的能力、
自动化覆盖和人工验收分别记录：M7 的标准 runner 必须实际提交 GPU 帧，Windows/macOS
工作流尚未像 Linux 一样强制断言这一点。当前性能 CI 已检查 CPU/Skia/GPU
五类固定场景的 p50/p95、submit/GPU wait、分配量及命令数，门槛为不超过 10%；
固定基准提交与候选在同一 runner 交替实测，双方五次，计时按 minimum/median 双重比较，
分配量取中位数，并归档完整元数据；计时回退还须超过 50 微秒噪声下限。
旧 `working-tree` 基线仅保留为历史样本，不参与门槛；headless 耗时不包含 present。
Designer 另有独立的 [DP-8 四类基线](perf-baselines/designer/README.md)，固定 `a1e012e`，
在 Linux CPU job 同机交错五轮，每轮 10 次 warmup 和 300 次测量，覆盖阶段计时、scoped
heap、重建次数和虚拟化范围。`cebe883` 的 Linux CPU job 已通过该门禁；
配置工作流或通过性能门禁不替代生产整帧 allocator 和真实窗口验收。
真实平台工作流还校验人工记录中的 Designer/allocator 附件与本次运行日志的 SHA256
一致，并要求 Designer 成功标记为唯一完整行；有效旧日志不能替代本次提交的窗口证据。
完整记录还须覆盖 Inspector 操作、Designer 编辑/保存/重开及 DPI/主题检查，并由各
必检读屏器完成 Designer 选择/定位和属性编辑/保存；遗漏这些流程或省略浸泡报告均拒绝。
Designer 外部文件冲突现已提供重载、另存和明确覆盖的应用动作；覆盖必须匹配观察时
的内容 revision，工程会在写页面前核对 manifest 和全部页面。同工程重载不会把旧
工作台同步进刚加载的页面。语义、键盘与失败回归属于 headless 证据，真实流程仍按上述
现场项登记；行为规范与视觉稿见 [`文件冲突恢复`](lumen-designer-file-conflict-design.md)。
文档撤销栈的合并还要求 750ms 内连续提交、相同非空 key、相同目标与连续选择；保存、
undo/redo 及新的 redo 分支打断合并。可控时钟用例覆盖暂停、边界、回退和混合事务，
本地默认 CTest 1139/1139、Release Designer 148/148 通过；用例链接旧实现时 3/3 失败。
这些结果属于文档事务 headless 验证，未增加真实桌面或读屏验收证据。
拒绝打开无效候选时，工作台现保留当前 DOM、选择、历史、文件 revision 和预览
session，应用的普通保存目标也保留；离线引用占位作为成功打开建立新会话。
新回归已复现旧实现在编译失败后替换 DOM 的问题，修复后本地默认 CTest
1142/1142、Release Designer 151/151 通过，仍不替代真实平台编辑/读屏记录。
编辑候选现在只准备一次编译，通过后才提交历史并发布；编译失败保留已有 redo 分支，
具体错误仍带节点和属性。暂存编译与发布的重建/帧代数、session 生命周期及历史回归
为 headless 证据，默认 CTest 1144/1144、Release Designer 153/153 通过，现场状态未变。
工程页面现按 documentId 保存各自的选择、历史、dirty 与文件 revision，切页不丢失
undo/redo，工程保存/另存只推进所有页面的保存检查点；快照不缓存运行时 frame，
失败切页保留原页面。独立打开/单页另存成功后退出工程并采用独立保存目标。
本轮 `[project-session]` 3 个用例、300 个断言，默认完整 CTest 1147/1147、Release
Designer 156/156 通过；`a592118` 三平台 CI 通过，完整人工记录仍待验。
诊断阶段现按实际错误来源保留 read/migrate/schema/save，并保留主文件或 `.bak`
来源；保存拒绝标记 block-save。显式 `--dump-diagnostics` 输出 GUI 聚合诊断的完整
JSON，工程错误也计入 CLI 数量；位置去重不再受换行字段碰撞影响。本轮阶段回归
6 个用例、163 个断言及 CLI 结构化输出检查通过，完整 CTest 1154/1154、Release
Designer 162/162 通过，三平台 CI 和完整人工现场记录仍按各自提交/现场证据登记。
G-D14 手柄改用设计坐标和完整按下/松开位移，Stack 的嵌套偏移、padding/margin
与 flow 布局偏移不误写位置；流式节点只调整尺寸，不被父节点当前自然尺寸截断。
视图变更取消拖拽，旧文档 revision 不得提交。新增六组 DPI/zoom、八个方向、
框选/辅助层和 undo/redo 的应用回归，默认完整 CTest 1159/1159、Release Designer
167/167 通过；`f209ed2` 三平台 CI 全通过，人工现场状态未变。
G-D16 图片资源按授权后的规范 URI 共享句柄并保留声明别名；读取/解码失败提供节点
诊断，pending 请求随编译关闭取消，ready 像素按新编译 token 验证后复用。撤销授权与
应用销毁释放句柄，不更改 DOM 或历史。共享 manager 的两个 renderer 现分别接收
upload/unload，完成通知覆盖全部共享窗口或指定的活动窗口，不同 manager 分别 pump。
实际 CPU 像素与 session 回归均通过，Windows 双预览像素分支不需要符号链接权限。
默认配置完整 CTest 1166/1166、启用 allocator 的 Release 完整 CTest 1173/1173，
Release `[designer],[resource-consumers]` 174 个用例、12655 个断言通过；`379275f`
三平台 CI 全通过，完整人工现场记录未增加。
Escape 在有输入法组合态时现先取消输入，保留属性/引用字段的原文选区与焦点，
后续返回再刷新编辑器和独立预览的路由标签；声明、revision 和历史保持一致。
两字段分支的应用回归 50 个断言、Release `[designer][ime]` 两用例 112 个断言通过，
默认配置完整 CTest 1167/1167、启用 allocator 的 Release 完整 CTest 1174/1174；
这仍是本批 headless 证据，不替代真实输入法或完整桌面记录。

### 实现批次与真实平台缺口（2026-09-30）

按 [`lumen-gui-completion-plan.md`](lumen-gui-completion-plan.md) 的缺口编号
（R0–R11）汇总 2026-09-29/30 M15–M18 实现批次及 2026-10-01 D3 决策记录的四态。实现已交付不等于平台
完成；「待现场」条目的平台、原因、降级与后续归属逐条登记于
[`platform-acceptance.md`](platform-acceptance.md) 的待验收登记表。

| 缺口 | 四态（2026-09-30） | 说明 |
| --- | --- | --- |
| R0 三桌面真实验收 | 接口已存在 + headless 已验证 | Linux X11/Wayland 有部分回环与浸泡记录；Windows/macOS 读屏、IME、跨应用剪贴板、透明合成、浸泡待现场 |
| R1 发布与安装收口 | Linux 链条较完整；Windows/macOS 部分 | 三平台 package/package-skia/package-skia-gpu CI 变体可构建；Windows/macOS 干净机器安装、启动、升级/卸载未记录 |
| R2 生产生命周期 | headless 已验证 | 恢复用例/资源重排队/字体异步生命周期有 headless 断言；Linux 1h 浸泡已有，Windows/macOS 浸泡待现场 |
| R3 OS 拖放 | 接口已存在 + headless 已验证 | OS 拖入归一化事件与应用内重排/列拖序 headless 契约通过；三桌面真实拖入 smoke 待现场（`drag_drop_os_receive` 已纳入必检清单）；拖出结构化不可用（SDL 3.2.10） |
| R4 桌面系统集成 | 部分接口已存在 | 全屏/置顶/OS 模态/托盘契约与 SDL 实现已有（headless 已验证）；全局快捷键 Linux X11 后端已交付（Xvfb XTEST 端到端）+ Win32 后端交付（RegisterHotKey + 消息专用窗口；**编译级验证 = windows.yml，真实按键验收待现场**；平台经 createPlatformBackend 分发）；Wayland/macOS 结构化不可用；macOS 原生菜单栏/交通灯未实现 |
| R5 文本与剪贴板深度 | headless 已验证 | G-3 剪贴板 MIME 数据层/图片与自定义格式/变更广播有 headless 断言；三桌面真实 IME 与跨应用复制粘贴待现场 |
| R6 开发者诊断 | dump 与调试图层已交付（headless 已验证） | `dumpRenderTree`/`dumpSemanticsTree`/`dumpStyleTree` + settings/gallery 三旗标 + 帧读数 HUD + bounds/damage 调试图层 + inspector 悬停检视图层（`--inspector`；信息面板 + 命中高亮；同日修复 HUD 经 overlay 槽位吞输入的缺陷——全部纯绘制层）2026-09-30 交付；HUD 已接入命令流 vector 存储分配计数/字节/峰值，并冻结 `FrameAllocationSource` 整帧 scope 契约和 unavailable 降级；Linux/glibc 可选原生 profiler 已有真实 C/C++/SDL headless 回归与 Wayland/Xwayland 单项 smoke；macOS/Windows 原生 CPU CI 已通过（来源与限制见 `lumen-frame-allocator-design.md`）；三桌面完整现场记录仍待补齐，命令流读数和 RSS 均不得替代整帧统计；inspector 钉住态与样式明细已交付（点击钉住/Esc 解钉，主键捕获为调试契约），现场 `inspector_tree_overlay` 现为必检 |
| R7 控件细节 | 按需池交付中 | 已交付 auto-hide 滚动条、Splitter 塌缩/KeepRatio、DataGrid 筛选接线、可编辑 ComboBox、DialogHost 便利层、ColorPicker、Grid 跨行列、菜单 F10/裸 Alt 单键切换 + 打开态 Alt+mnemonic 顶级切换、List/Tree 行内编辑（2026-09-30）；RTL 镜像、双轴联滚、触摸长按唤起等在池 |
| R8 复杂文本 | 未启动（按需） | 保持 UAX#9 子集 + 逐 grapheme shaping；HarfBuzz/完整 UBA/TextSpan 待产品需求触发 |
| R9 框架使用效率 | 部分交付 | `examples/template` 脚手架与 Gallery 样本已有；`examples/common/example_kit.h` 首批提取 mutedLabel/errorText/statusLine（2026-09-30，两应用逐字节重复收敛为单点）；页面壳（sectionCard 级）仍按需 |
| R10 文档与证据同步 | 进行中 | 本表与待验收登记即该缺口的 2026-09-30 批次；后续状态变化须同一变更内更新 |
| R11 D3 L0 可编辑设计器 | L0 应用出口已交付 + headless 已验证 | DP-1=B、DP-3=L0、DP-4=独立 preview/session 已于 2026-10-01 决定；DesignerApp 已接通 12 类型工具箱、属性/命名引用/结构编辑、undo/redo、设计文件和工程文件的打开/保存/另存为/新建路径及文件对话框；工程保存已覆盖全页面 revision 预检、目标工程根重定位、页面失败加载状态回滚与诊断保留、工程根内声明资源复制和源/目标符号链接越界保护，工程打开会诊断缺失资源/非普通文件/越界符号链接；F6 `designer_memory_peak` 已记录预览打开/编辑重建/VirtualList layout-paint 的 scoped heap 峰值；platform-acceptance 要求四平台的原生窗口 smoke、编辑/保存/重开、DPI/主题和 Designer 自身读屏流程，本机 Wayland 单项启动已通过，完整现场证据仍待 |

### 预乘 alpha 联调历史记录（2026-09-20）

2026-09-20 预乘 alpha 联调补充：CPU 帧/缓存与 Skia 读回准确声明
Premultiplied / Opaque；三种图片输入模式可跨后端及设备重上传。
Windows direct3d11 texture 与原生 software 的 resize/最小化恢复已经实测。
但 Windows 原生 software 为 XRGB8888，**不支持逐像素透明**；present 成功
仅代表提交。Windows texture 的合成器视觉、Linux X11/Wayland 和 macOS
桌面透明验收仍待运行；固定 SDL 在 Wayland/macOS 没有严格 native software
framebuffer，当前无 GPU 的软件窗口回退仍有限制。证据与逐路径说明见
[P3 联调记录](perf-baselines/premultiplied-alpha-2026-09-20/P3.md)，不据此提升三平台发布状态。

P4/P5 交付验证：Windows CPU Debug/Release 各 755/755、Skia raster Release 768/768、
GPU Release 779/779，无跳过；GPU 三模式图片经历真实上下文销毁重建及 CPU 重上传。
这不是 Linux 运行时 GPU/present 故障注入，该轮未验该路径。62 个视觉样本在统一预乘表示下
alpha 精确一致，普通 RGB 最大差 2；CPU/Skia 各三组正式配对及收益/代价见
[P4 报告](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)。GPU 性能未测。
公开像素结构需要重编译，`pixels()` 返回实际模式；命令写 v7、读 v6/v7，
Gallery 默认导出保持 straight，见 [迁移说明](lumen-alpha-migration.md)。

同日续验补充 Linux **WSL Ubuntu 24.04 / GCC 13.3 CPU Debug** 构建与 headless/dummy 测试：
754 项中 753 通过、1 项因无系统字体跳过、0 失败；修复非 Apple 构建误编 macOS `.mm`
文件及通知能力的旧测试假设。没有 Linux 桌面会话、Skia/GPU 或合成器新证据，
上述实窗待验项不变。Windows CPU Release 756/756（含一个基准工具 CTest）、
Skia/GPU Release 779/779，无跳过；详见
[续验记录](perf-baselines/premultiplied-alpha-2026-09-20/follow-up.md)。

## 历史移动实验内容（暂不实现，已冻结）

| 平台 | 当前范围 | 已有代码 | 验证边界 |
| --- | --- | --- | --- |
| Android | 暂不实现，已冻结 | 保留 `lumen-mobile-host` 通用状态机；JNI/NativeActivity 胶水不实现 | Linux 的 mobile-core job 只检查 SDL-free 通用代码，不是 NDK 或设备验证，也不是任务入口 |
| iOS | 暂不实现，已冻结 | 保留同一 `MobileHostSeam` 状态机；Objective-C++ 胶水不实现 | macOS 的 mobile-core job 只检查 SDL-free 通用代码，不是 iOS 工程或 Simulator 验证，也不是任务入口 |

原生接入、软键盘、移动字体/绘制管线、移动页面、GPU、无障碍与发布均不在当前
设计和验收范围，不进入当前或后续自动计划。已有 `createMobileFontManager`、
`FontBackend::Mobile` 和默认字体栈分支也不能作为移动端文本可用的证据。

现有源码、构建开关、测试和 Linux/macOS mobile-core CI job 可作为历史兼容性检查保留；
不得由此新增移动实现任务。桌面触屏、Touch density、
窄窗口和通用安全区指标仍属于桌面可用性设计。

## 后端与能力

| 能力 | 提供方 | 降级行为 |
| --- | --- | --- |
| CPU 光栅 | `CpuRenderer`（窗口经系统字体绘制真实字形，headless/无字体时确定性占位） | 无需降级；CPU-only 构建不依赖 SDL 实现库与桌面会话 |
| Skia 光栅 | `SkiaRenderer`（可选 `LUMEN_ENABLE_SKIA`） | 未编入时能力报告 `backendName=cpu`，应用安全运行 |
| Skia GPU | `SkiaGpuRenderer`（可选 `LUMEN_ENABLE_GPU`） | counter 的探测/初始化失败路径回退 CPU；运行时故障在 Skia 构建中改用 Skia 软件光栅以保持文本连续性；回退钩子和诊断见路线图 P3 |
| 文本 shaping | `lumen-text` + `SkiaFontManager`（可选 `LUMEN_ENABLE_SKIA`，封装字体族/回退/度量/shaping；公共接口无 Skia 类型）或 `SystemFontManager`（CPU 窗口默认：Windows 优先 GDI 系统字形，其他平台及 GDI 回退经 stb_truetype 读取系统字体，Windows 雅黑优先） | CPU-only 构建或无系统字体时回退 `PlaceholderFontManager`，布局/编辑照常（`fontDiagnostic`/工厂诊断明确报告）；布局与绘制共享同一份 shaped 结果，光标/选区不跨后端漂移 |
| 编辑撤销 | `text::EditingHistory` + `InteractionController`（Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y；连续单字输入/删除合并，IME 提交为单事务，preedit 不进栈） | 只读字段与 preedit 期间拒绝撤销；栈按字段 bind 隔离，容量 100 |
| 应用壳 | `lumen-app`（`app::AppShell` 帧管线 + `app::runApp` 单/多窗口主循环；M2） | 示例只保留 build/状态/handler；支持 Fake host、外部 renderer 和应用回退钩子；GPU 失效可重建软件窗口 |
| 剪贴板 | `platform::Clipboard` / `core::ClipboardProvider` | runApp 启动时接入宿主剪贴板（Ctrl+C/V；宿主不可用保持未注入）；`setText` 失败返回 false，编辑状态不丢 |
| 语义桥接 | `AccessibilityBridge`（接口 + Recording 桥 + AppShell 每帧 identity diff/焦点/action 回执驱动，M5 收口） | Windows UIA、Linux AT-SPI2、macOS NSAccessibility provider 在可选编译开关开启时编入；无桌面服务时能力如实降级，真实屏幕阅读器回环另行验收 |
| 可访问性设置 | `PlatformCapabilities`、`AccessibilitySettings` 与逐项 `AccessibilityOverrides` | SDL 宿主初始化和运行期间查询；`runApp` 自动跟随所有未覆盖字段，重复快照不重绘；关闭跟随、服务缺失和平台键缺失均有明确降级 |

## 当前能力与已知限制（映射到自用路线图里程碑）

- 双向文本为 UAX#9 确定性子集（强/弱/中性类近似，无显式嵌入控制、镜像
  括号与数字定形）：纯 RTL/LTR 与常见混合段落正确，完整 UBA 属后续版
  本；grapheme 边界仍是唯一编辑索引（M1 已收口，见 M1 完成记录）。
- Skia shaping 为逐 grapheme cluster（无 HarfBuzz 连写/合字）：拉丁/
  CJK/希伯来/阿拉伯基本形正确，复杂脚本合字后续增强。
- VirtualList 已落地（M3：可见区物化/实测 extent 修正/锚点稳定），虚拟化仍为
  纵向；M10 已接入触摸/指针拖动与确定性惯性滚动。ScrollView 水平轴、
  横纵滚动条拖动与嵌套滚轮路由已实施；同一视口双轴联滚、水平虚拟化、
  RTL 镜像和 auto-hide 未实现。Gallery 已有超宽卡片水平滚动演示区，详见
  [滚动设计](lumen-scroll-design.md)。
- Grid 为纵向网格（无横向滚动）；子项可声明跨行列（`withGridSpan`，
  阶段C 2026-09-30——占位流式放置 + 跨行差额入末跨行，span=1 与 M3
  基线逐字节同几何，见
  [Grid 跨行列设计](lumen-grid-span-design.md)）；Image 需应用侧资源管理器
  驱动加载（框架不管理异步资源生命周期）。
- 平台原生无障碍 provider 已在 M13 编入三桌面目标并有 headless 回归；Linux
  AT-SPI2、macOS NSAccessibility 和 Windows UIA 的真实屏幕阅读器人工回环
  仍需单独验收，不能由 Xvfb 或 headless CI 代替。
- 应用主循环/damage 管线已收敛到 `lumen-app` 应用壳（M2）：新工具页只需
  提供 build/状态逻辑；`runApp` 支持单窗口兼容入口和多个 `AppWindow` 绑定，
  按 `HostEvent.window` 隔离输入、DPI、IME、renderer、语义桥和帧调度。
  宿主窗口生命周期仍由调用方拥有；最后一个运行时关闭后主循环结束。
- Splitter 塌缩/KeepRatio（M17，2026-09-30）：`setCollapsible/setCollapsed`
  （塌缩钉 minLeading，移离 min 自动解除）+ Enter/Space 切换（不可塌缩
  回退激活路径）；ResizeBehavior 双行为（KeepRatio 等比缩放）。
- DataGrid 筛选接线（M17，2026-09-30）：`requestFilter`（编辑守卫入口）+
  `setFilterActive`（无结果空态文案切换）；筛选面板 UI 仍由应用提供。
- headless 语义树导出（M18，2026-09-30）：`buildSemanticsSnapshot` +
  `dumpSemanticsTree` + settings `--dump-semantics`（与 --dump-tree 同链）。
- auto-hide 滚动条（M17，2026-09-29）：`withAutoHideScrollbar` 声明式开启；
  滚动活动（滚轮/键盘/拖动/惯性）打开 800ms 可见窗口（
  MotionTokens.scrollbarAutoHideMs），到期隐藏（`scrollbarHidden` 绘制跳过，
  命中区保留——悬停重显）；reduceAnimation 不归零（可发现性行为）。
- headless 树导出（M18 首块，2026-09-29）：`app::dumpRenderTree`（确定性）
  + settings `--dump-tree`；语义 dump/inspector/帧统计 overlay 为后续增量。
- 帧读数 HUD 与样式导出（R6 首批，2026-09-30）：`dumpStyleTree` +
  settings/gallery `--dump-style`（与 --dump-tree 同树同序，`style:` 前缀
  行 + `#rrggbbaa` 颜色）；gallery 补齐 `--dump-tree`/`--dump-semantics`
  平价；`app::makeFrameStatsOverlay` + `RunOptions.frameDebugOverlay`
  （settings/gallery `--frame-overlay`）——reconcile/layout/paint/submit/
  GPU wait、fps、节点/命令数、命令流存储分配与当前 renderer，读数滞后一帧，
  全子树排除语义与焦点；默认关闭零额外帧、frame hash 不变（单测断言）。HUD
  的命令流存储分配读数只覆盖 RenderCommandList vector 容量增长；整帧 heap 行通过
  `FrameAllocationSource` 显示真实来源或 unavailable，当前平台 source 尚未接入；inspector
  与 bounds/damage overlay 仍为后续增量。
- 示例组件 kit 首批（R9，2026-09-30）：`examples/common/example_kit.h`
  ——Gallery/Settings 逐字节重复的 `mutedLabel`/`errorText` 收敛为单点
  （相对包含，调用点零改动、输出不变——既有示例测试全绿即回归证据）；
  新增 `statusLine`（"Label: value" 状态摘要，空值占位 "-"）并接入
  settings 的 Volume/Progress 行；契约测试 2 例（Theme token 覆盖/格式
  与占位）。页面壳（gallery 的 sectionCard/panelCard 语言）为应用视觉
  专属，保持应用内——框架级组合组件按需再评估。
- 帧读数 HUD 修复与 inspector 悬停检视（R6 二批，2026-09-30）：HUD 早
  前经视觉 overlay 槽位承载——`eventTree()` 在 overlay 活跃期整体切换，
  非交互 HUD 会吞掉全部应用输入（阶段2缺陷，本批发现并修复：HUD 改
  `makeFrameStatsLayer` 纯绘制合成树，回归测试断言点击仍激活）。新增
  `--inspector`（`inspector_layer.h`）：悬停检视最深命中节点（信息面
  板 type/key/identity/bounds/style/scroll/text/flags + 2px accent 命中
  高亮；文案与 dump 工具同口径）；指针移动驱动的重绘仅开启时出现。
  三个调试图层与 HUD 全部不经 overlay 槽位——输入零影响是构型保证。
- inspector 钉住态与样式明细（R6 四批，2026-09-30）：开启 inspector 后
  主键点击被检视器捕获——钉住命中节点（identity 跨重建稳定；再点换
  钉、点击空区解钉、Esc 解钉），面板追加 common 段样式明细行（bg/fg/
  border 十六进制 + radius/borderWidth/min，--dump-style 同口径）与
  [pinned]/[focused] 标记；右键/中键与滚轮、键盘不拦截（应用路径保持）。
  钉住捕获是显式调试契约：仅主键、仅 inspector 开启期。
- 窗口与系统集成（M16 实现批次 2026-09-29）：全屏（`toggleFullscreen` +
  WindowFullscreenEntered/Exited 事件）、置顶、OS 模态（SDL_SetWindowParent
  + SetWindowModal）契约与三桌面能力位；系统托盘经 SDL_tray（菜单激活回灌
  TrayActivated）；windows/macos 新增 package-skia-gpu 包变体。当前四态 =
  接口已存在 + headless 已验证（Linux）；真实平台 smoke 与 macOS 原生
  菜单栏/交通灯未做。
- 全局快捷键 Win32 后端（R4 续批，2026-09-30）：`global_hotkeys_win.cpp`
  ——RegisterHotKey + 消息专用窗口（HWND_MESSAGE 父级；WM_HOTKEY 经
  SDL 泵同线程 DispatchMessage 到达窗口 WndProc，GWLP_USERDATA 找回
  属主入队，pollEvent 消费——托盘同模式；每后端实例独立窗口与注册
  表，多 host 共存）。`createPlatformBackend` 平台分发（_WIN32 → Win32
  / __APPLE__ → 结构化不可用 / 其余 → X11 会话探测）。MOD_NOREPEAT 对
  齐 grab 不重复语义；注册预算 4096。**四态 = 接口已存在 + 编译级验证
  （windows.yml 门禁）**；真实按键验收（消息泵→UI 事件、GetLastError
  冲突码）待登录 Win32 会话现场。
- 全局快捷键 X11 后端（R4/M16 增量，2026-09-30）：`global_hotkeys_x11`
  内部接缝——独立 X 连接 `XGrabKey`（锁定键 8 组合各 grab、事件匹配剔除
  锁定掩码；BadAccess 临时错误处理器 + XSync 检测冲突并回滚）；事件经
  `pollEvent` 转 `GlobalHotkey`（text=注册 id，window=owner）。Wayland
  会话（XDG_SESSION_TYPE/WAYLAND_DISPLAY 探测）结构化不可用——XWayland
  grab 只覆盖 X11 客户端，如实不宣称系统级。当前四态 = 接口已存在 +
  headless 已验证（映射纯函数单测）+ X11 协议级已验证（Xvfb XTEST 端到
  端 `LUMEN_GLOBAL_HOTKEY_E2E=1`：注册→合成按键→事件送达→注销不泄
  漏）；Win32/macOS 后端未实现（能力位 false + 结构化 Unavailable），
  真实桌面（非 Xvfb）按键验收待现场。
- 拖放（M15 实现批次 2026-09-29）：OS 拖入经 `HostEvent` DragEnter/
  Move/Drop/Leave 归一化交付（SDL DROP_* 翻译；COMPLETE 无负载合成
  DragLeave），`startDrag` 结构化 Unavailable（SDL 3.2.10 无拖出 API，
  `dragDropStart` 能力位如实 false）；应用内 List/DataGrid 行重排与
  DataGrid 列拖拽经 core 会话状态机（arm/阈值/仲裁表），Alt+↑/↓ 与语义
  MoveUp/MoveDown 提供非指针路径。当前四态 = 接口已存在 + headless 已
  验证（契约见 [`lumen-drag-drop-design.md`](lumen-drag-drop-design.md)）；
  三桌面真实 OS 拖入 smoke 未做，拖出待 SDL 升级。
- 平台服务已统一接口（M4：文件选择/OpenURL/通知/光标/图标 + 能力报
  告）；M12 已收口原生后端——通知与强调色走 native seam（Win32/
  DBus/AppKit），SDL 系统主题输入经 `SDL_GetSystemTheme` +
  `SystemThemeChanged` 事件接入；当前三平台 CI 证据见上方验证快照。高对比/
  减少动画/字体缩放已接入独立采样与变化广播，应用逐项覆盖优先；实际 OS
  设置面板的人工切换验收仍需真实桌面，隔离 portal/假宿主测试不代替它。
- M6 视觉系统 V3 已收口（图标/阴影/滚动条 token 路径、六控件、
  ThemeScope、组合校验器）；M10 已收口转场动画驱动（transitionAlpha
  整节点透明度、Dialog/Navigator 淡入淡出、状态色过渡 opt-in）；
  M11 已收口 Tooltip hover 延迟显隐与 Dropdown 浮动菜单
  （框架级 overlay + Up/Down/Enter/Esc 键盘导航）。
- M11 v0.4 视觉方向已收口：ThemeDirection 四方向（CoreDark 默认 /
  InkLinen / AuroraSignal / UtilityContrast），方向与深浅/高对比/字体
  缩放/密度正交组合经 fromSettings 派生；Aurora 为扁平近似（玻璃/
  渐变/光晕不做，见 roadmap M11 已知限制）。
- M7 实现已交付：三平台 GPU CI 已配置；partialSubmit 历史评估为 1.16×，
  当前仍报告全帧提交。Element move 管道当时的性能门槛为 6/6；该历史
  结果不代表当前持续性能门槛，实际 GPU 提交与性能 CI 缺口见验证快照。
- M8+M12 便携发布已收口：install/CPack/CI package job + 解包冒烟 +
  Linux AppImage（linuxdeploy）与 macOS Lumen.app 骨架 + package-skia
  （三平台）/package-skia-gpu（Linux llvmpipe）变体；当前 CI 运行成功，
  Windows/macOS GPU 包与 CPack Bundle 留后续。AppImage 生成和 `.app` 内
  headless 启动不替代干净桌面人工启动验收。当前安装清单已覆盖
  `lumen-render` 与其余桌面公共静态库，并随开发归档安装
  `LumenConfig.cmake`/`LumenTargets.cmake`（包含 SDL3 头文件和运行时目标）；
  因此运行时 AppImage/`.app` 与外部 SDK 的职责保持分离。
- 移动方向暂不实现并冻结；M9 只保留状态说明，不作为桌面版本的待完成项。
