# Lumen 轻量持久化助手设计（G-7）

> 状态：已实现（2026-09-30；gap-backlog G-7，P3）。
> 动机：每个工具重复实现原子保存/加载/损坏恢复/版本迁移样板；`StateStore` 为字符串内存表，落盘由应用手写。
> 代码：[`include/lumen/core/preferences.h`](../include/lumen/core/preferences.h)、`src/core/preferences.cpp`（纯 core，无 UI/平台依赖）。
> 测试：`tests/preferences_tests.cpp`。

## 1. 定位与边界

**应用层边界内的薄助手**（百行级）：键值 + 类型转换 + 变更通知 + 原子写 + 版本字段 + 损坏降级。**不进**网络/同步/加密/跨进程协调（维持主路线图 §1.2 排除承诺）；不与 `StateStore` 合并（StateStore 是 UI 绑定内存表，Preferences 是落盘副本——应用桥接两者）。

## 2. 格式与原子性

- 自写行格式（无外部依赖）：
  ```
  # lumen-prefs v1
  version=<int>
  <escaped-key>=<escaped-value>
  ```
  转义：`\n`/`\\`/`\=`（键与值同规则）；UTF-8 字节直通。
- 原子写：写 `<path>.tmp` → `rename`（POSIX 原子替换）。**写一半崩溃只留 .tmp 残留，主文件恒完整**——与 G-2 的崩溃兜底正交（无脏标记，写失败返回 false 不破坏内存态）。
- 加载：文件缺失 = 首次运行（ok + 空表）；头缺失/非法行 = 损坏降级（false + 空表，应用可提示重建）；`version` 键由助手管理（`setVersion/version`，迁移解释权在应用）。

## 3. 类型与通知

- 存储统一字符串；`setInt/getInt`、`setBool/getBool`、`setDouble/getDouble`、`setString/getString`。类型不匹配或解析失败 = fallback 降级（bool 接受 "true"/"1"/"false"/"0"）。
- 变更通知：**值实际变化才通知**（幂等写不通知）；`remove`/`clear` 同样通知（clear 以空 key）；`subscribe/unsubscribe`（id 制，UI 生命周期安全）。

## 4. 验证

类型往返与降级、通知幂等性、保存/加载往返（含多行/特殊字符）、损坏降级（垃圾头/非法行）、缺失文件首次运行语义、原子写（坏 tmp 不影响主文件/正常保存清 tmp/失败不破坏内存态）、转义往返。全部 headless。

## 5. 与 G-6 组合

`lumen-template` 新应用脚手架（G-6）以 Preferences 为持久化首个消费者：窗口位置记忆（G-8）与「上次打开」等轻状态直接复用。
