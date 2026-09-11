#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "lumen/core/windowing.h"

namespace lumen::platform {

// v0.3 阶段8E (plan §2.1/§4 8E): 移动端 host 接缝。
//
// Android native host（JNI/NativeActivity）与 iOS Objective-C++ host 只
// 负责把平台回调翻译到这里；本状态机给出可 headless 测试的确定性语义：
//   - surface attach/detach/resize：先更新 WindowMetrics 再发事件；detach
//     期间不销毁状态树，只暂停提交（plan §3.1/§7 移动端生命周期）。
//   - pause/resume：AppLifecycle 映射（Background/Suspended）；resume 时
//     只有 surface 已重连才恢复 Active。
//   - 触摸坐标：格式由 Config.touchNormalized 决定，统一转换为逻辑坐标
//     HostEvent。
//   - 返回/关闭请求：统一为 WindowCloseRequested 事件（8D Navigator 的
//     handleBack 消费）。
// 平台 SDK 类型不出现在此头文件；native 胶水是各平台的可选编译目标。

class MobileHostSeam {
  public:
    struct Config {
        // true 时输入为 0..1 归一化坐标；false 时输入为 drawable 像素坐标，
        // 由接缝按 deviceScale 转成逻辑坐标。
        bool touchNormalized{true};
    };

    // Config 的 NSDMI 在包围类完成前不可用作默认实参（CWG 1397，
    // GCC/Clang 拒绝 `Config config = {}`），默认构造经委托实现。
    MobileHostSeam() : MobileHostSeam(Config{}) {}
    explicit MobileHostSeam(Config config);

    // --- surface 生命周期（UI 线程调用） ---
    // surface 创建/重连：metrics 先行，SurfaceReattached + Resize 入队。
    void surfaceCreated(float widthPixels, float heightPixels,
                        float deviceScale, core::EdgeInsets safeArea);
    // 尺寸/旋转/安全区变化：metrics 先行，Resize 入队。
    void surfaceChanged(float widthPixels, float heightPixels,
                        float deviceScale, core::EdgeInsets safeArea);
    // surface 销毁（暂停期间可能发生）：SurfaceDetached 入队；状态保留。
    void surfaceDestroyed();

    // --- 应用生命周期 ---
    // 后台（Activity.onPause / UIApplicationWillResignActive）。
    void appPaused();
    // 前台恢复：surface 已重连才回到 Active（否则等 surfaceCreated）。
    void appResumed();
    // 系统内存告警/强制挂起建议。
    void appLowMemory();

    // --- 输入 ---
    // 触摸坐标 → 逻辑坐标事件；坐标格式由 Config.touchNormalized 决定。
    void touchDown(std::uint32_t pointerId, float x, float y);
    void touchMove(std::uint32_t pointerId, float x, float y);
    void touchUp(std::uint32_t pointerId, float x, float y);
    void touchCancel(std::uint32_t pointerId);
    // 平台返回键/关闭请求（Android back button / iOS 无）。
    void backRequested();

    // --- 事件泵（与应用主循环对接） ---
    bool pollEvent(core::HostEvent& out);
    [[nodiscard]] core::WindowMetrics metrics() const { return metrics_; }
    [[nodiscard]] core::AppLifecycle lifecycle() const {
        return lifecycle_;
    }
    [[nodiscard]] bool surfaceAttached() const { return surfaceAttached_; }
    [[nodiscard]] bool canRender() const {
        return surfaceAttached_ &&
               (lifecycle_ == core::AppLifecycle::Active ||
                lifecycle_ == core::AppLifecycle::Inactive);
    }
    // 诊断：最近一次生命周期/surface 转换被拒绝的原因（不变量检查用）。
    [[nodiscard]] const std::string& lastNotice() const { return lastNotice_; }

  private:
    void push(core::HostEvent event);
    void setLifecycle(core::AppLifecycle next);
    void updateMetrics(float widthPixels, float heightPixels,
                       float deviceScale, core::EdgeInsets safeArea);
    [[nodiscard]] core::Offset toLogical(float x, float y) const;

    Config config_{};
    core::WindowMetrics metrics_{};
    core::AppLifecycle lifecycle_{core::AppLifecycle::Launching};
    bool surfaceAttached_{false};
    bool foregroundRequested_{false};
    std::uint64_t nextTimestampMs_{0};
    std::deque<core::HostEvent> queue_{};
    std::string lastNotice_{};
};

// 阶段标识（8E 移动 host 接缝）。
[[nodiscard]] const char* mobileHostStageName();

}  // namespace lumen::platform
