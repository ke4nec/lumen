# 预乘 alpha 交付后复查与补验

基点 `7d226b4`，2026-09-20。继续复查 P0–P5 的像素边界与验收工具，并利用本机 WSL
补充 Linux 编译/headless 覆盖；本轮不修改渲染生产算法或原性能/视觉基线。

## 汇总工具修复

复现两项问题：单组 JSON 的 revision 与 manifest 不同仍可通过；后续组 before/after
同时多出一个计时阶段时，比较器仅对照该组两端，输出又只枚举首组阶段，导致新增阶段被遗漏。

现在先核对两份 manifest 的平台、机器、处理器和后端，再逐报告绑定 revision、可选的
内嵌 commit、后端、基准名称及文件组号。每个场景的所有组使用同一计时阶段集合；缺失配对
和残留 `.pending` 明确报错。此检查只保证记录内部一致，不代替采样时的二进制 SHA 核对
或主机负载控制，也不因历史可执行文件后来重建而否认已经冻结的采样。

新增 10 个标准库 unittest：正确配对的逐组百分比与两种中位数、混入其他提交、内嵌提交
冲突、组号冲突、不同机器、两端同步换后端、未登记的基准、跨组阶段变化、缺失配对和未完成标记。
两个缺陷用例在 `7d226b4` 的旧汇总器上均失败，修复后全部通过。tests/benchmarks 启用且
有 Python 3 时由 CTest 自动运行；Python 仍是工具测试的可选依赖。

原 CPU `build-alpha-after/p4-final-paired` 与 Skia `build-alpha-skia-after/p4-paired`
各 90 次运行全部通过新校验。重新计算的 `summary` 和 `runs` 与归档 P4 JSON 精确相等；
原报告、超限标记和量化容差均未修改。复现及核对日志：
`build-alpha-after/alpha-review-report-check.log`。

## Linux 构建阻塞修复

首次完整构建在 `native_services_mac.mm` 失败：GCC 按 `.mm` 扩展名先启动 Objective-C++
前端，报 `cannot execute cc1objplus`，尚未执行源码的 `__APPLE__` 条件编译。
该文件从既有 `d943eac` 原生服务变更起被无条件列入桌面源文件清单，与预乘算法无关。
现在仅在 `APPLE` 时加入该文件，Windows/Linux 不再需要 Objective-C++ 工具链；
macOS 源码与选择路径保持原状，本轮没有 macOS 编译证据。原失败日志保留在
`build-alpha-linux-review/build-before-platform-fix.log`，修复后不再生成该源文件的 Linux 编译规则。

首次 Linux CTest 随后发现旧 `sdl_host_services_degrade_structurally` 把所有非 Windows
通知能力断言为 false，与 M12 已有 Linux(libdbus)/macOS 原生实现及测试注释冲突。
修正为只对没有原生实现的平台要求 false；Windows 原断言保留，Linux/macOS 不根据
dummy 视频驱动推断原生服务能力。没有发送真实通知，也没有更改通知实现或能力位。
失败记录保留在 `build-alpha-linux-review/ctest-before-fixture-fix.log`。

## 验证环境与记录

- Windows CPU Release：重新配置/构建后完整 CTest **756/756**，含 10 项 Python 回归组成的
  一个 CTest；平台源文件修复后的日志 `build-alpha-after/ctest-alpha-review-final.log`。
  原有 755 项 C++ 门槛均通过，无跳过。
- Windows Skia/GPU Release：共享平台构建修复后完整 CTest **779/779**，无跳过，日志
  `build-gpu/ctest-alpha-review-final.log`。该配置未启用 benchmarks，Python 工具测试不计入其数量。
- Linux：本机 WSL Ubuntu 24.04，GCC 13.3.0、CMake 3.28.3、Python 3.12.3。
  单独以 `-std=c++20 -Wall -Wextra -Wpedantic -O0 -g` 编译 `cpu_renderer.cpp` 成功，
  验证 GCC Debug 下的强制内联声明与调用。
- Linux 完整桌面 CPU Debug 配置构建成功，包含 counter/settings/gallery 及三个 alpha 工具。
  完整 CTest 754 项：**753 通过、1 跳过、0 失败**，耗时 42.25 秒。
  `system_font_text_clip_preserves_cjk_and_mixed_ink` 报 `No system fonts available`；
  未把缺少字体的测试计为已验证。预乘数值/模式/命令/资源/Preserve、dummy 窗口矩阵、
  Gallery 确定性字体及 Python 工具检查均运行通过。日志为
  `build-alpha-linux-review/build.log` / `ctest.log` / `Testing/Temporary/LastTest.log`。

Linux 构建使用忽略目录 `build-alpha-linux-review`，Debug、tests/examples/benchmarks ON、
Skia/GPU OFF；通过 `FETCHCONTENT_SOURCE_DIR_SDL3/CATCH2/STB` 复用仓库已有固定依赖源码，
没有安装系统包、更改依赖版本或启用 mobile-core。

可从仓库根目录在现有依赖齐备的 Linux 环境复现：

```sh
cmake -S . -B build-alpha-linux-review -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON -DLUMEN_BUILD_BENCHMARKS=ON -DLUMEN_ENABLE_SKIA=OFF -DLUMEN_ENABLE_GPU=OFF
cmake --build build-alpha-linux-review --parallel 4
ctest --test-dir build-alpha-linux-review --output-on-failure --parallel 6
```

本轮实际通过 `wsl.exe -d Ubuntu24.04 --` 调用上述命令，并在配置时把三个 FetchContent
源目录指定到 `/mnt/d/prj/nono/lumen/build/_deps/{sdl3,catch2,stb}-src`；这些仅复用源码，
不复用 Windows 编译产物。GCC 编译保留了仓库和固定 SDL 的既有警告，不宣称无警告构建。

WSL 未提供 DISPLAY/Wayland 桌面会话；headless 和 dummy 驱动测试不能替代 Linux
X11/Wayland 透明合成、实际窗口生命周期或 GPU 运行时故障注入。Windows 合成器视觉和 macOS
也没有本轮新证据，原 P3/P5 的平台限制继续保留。
