#pragma once

// G-8（gap-backlog）：单实例激活助手。
//
// 第二次启动聚焦已有窗口：本地 unix domain socket 绑定（运行时目录 +
// 应用名）。首个进程 Primary = bind 成功并起 accept 线程监听激活请求；
// 后续进程 Secondary = connect 发送 "activate" 后退出进程（应用在
// main 早期调用，Secondary 即刻返回退出）。激活回调在后台线程触发
// ——应用负责投递 UI 线程（raiseWindow 经 host 线程安全封装或事件）。
//
// 平台与降级（docs/lumen-window-experience-design.md §4）：POSIX 完整
// （Linux/macOS）；socket 目录不可用/bind 失败因非占用原因 → 结构化
// 降级 SecondaryNotifyFailed/Unavailable，允许应用选择继续运行（绝不
// 因助手失败丢窗口）。Windows 命名锁 seam 为后续增量。纯本地 IPC——
// unix socket 无网络栈参与，不构成网络能力承诺（路线图 §1.2）。

#include <cstdint>
#include <functional>
#include <string>

namespace lumen::core {

class SingleInstanceGuard {
  public:
    enum class Status : std::uint8_t {
        Primary,               // 首个实例：已绑定并监听
        SecondaryActivated,    // 已有实例且激活请求已送达
        SecondaryNotifyFailed, // 已有实例但通知失败（socket 半开等）
        Unavailable,           // 运行时目录/socket 不可用（结构化降级）
    };

    struct Config {
        std::string appName{};      // socket 文件名（含命名空间）
        std::string socketDirectory{};  // 空 = XDG_RUNTIME_DIR|/tmp 惯例
        // Primary 收到激活请求时触发（后台线程；应用自行投递 UI 线程）。
        std::function<void()> onActivateRequest{};
    };

    // 进程生命周期一次调用（main 早期）。Primary 的监听线程随进程退出
    //（守护 socket 文件在析构时清理——应用持有返回的守卫对象）。
    [[nodiscard]] static Status acquire(const Config& config);
};

}  // namespace lumen::core
