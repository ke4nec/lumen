#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lumen/core/clipboard.h"
#include "lumen/core/windowing.h"
#include "lumen/platform/platform_window.h"

namespace lumen::platform {

// v0.3 阶段8A (plan §3.1): 应用宿主契约。
//
// 把 v0.2 PlatformWindow 的窗口、事件和呈现职责拆成三个稳定边界：
//   ApplicationHost —— 初始化/退出、事件泵、生命周期与全局服务；
//   窗口管理（create/destroyWindow、windowMetrics、WindowId）；
//   PlatformServices —— Clipboard、TextInputSession、鼠标光标等。
// 旧 PlatformWindow 工厂保留为过渡适配（counter 与既有测试继续直接
// 使用）；SDL3/macOS/移动端 host 只负责填充归一化事件字段。

struct WindowDesc {
    std::string title{"Lumen"};
    int width{800};
    int height{600};
    bool resizable{true};
    // 跟随系统缩放，使 drawable 像素跟踪逻辑尺寸（plan §2）。
    bool highPixelDensity{true};
    // OpenGL 窗口供 GPU 适配创建上下文（v0.2 阶段7C）。
    bool opengl{false};
};

// 剪贴板服务（实现 core::ClipboardProvider，交互层直接消费）。不可用
// （无桌面会话/权限被拒）时 setText 返回 false、text() 返回空；应用状
// 态不得因此丢失（plan §2.3 不变量）。
class Clipboard : public core::ClipboardProvider {
  public:
    ~Clipboard() override = default;
};

// TextField 编辑状态快照（grapheme cluster 索引，plan §3.2）。平台转换
// 只发生在适配层；候选词锚点使用 caretRect（逻辑坐标）。
struct TextInputEditingState {
    std::string text{};
    std::size_t selectionBase{0};
    std::size_t selectionExtent{0};
    bool hasComposing{false};
    std::size_t composingBase{0};
    std::size_t composingExtent{0};
    core::Rect caretRect{};
};

// 文本输入会话：commit 由 TextInput 事件到达，preedit 由 TextEditing
// 事件到达；这里承载平台侧会话控制与编辑状态回放（候选词定位）。
class TextInputSession {
  public:
    virtual ~TextInputSession() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool active() const = 0;
    // 每次编辑状态变化后同步给平台 IME。
    virtual void setEditingState(const TextInputEditingState& state) = 0;
};

// 平台能力报告（plan §2.3：能力必须可查询并有安全降级）。
struct PlatformCapabilities {
    bool clipboard{false};
    bool textInput{false};
    bool ime{false};
    bool keyboard{false};
    bool mouse{false};
    bool touch{false};
    bool multiWindow{false};
    // 语义桥接可用（阶段8C）。
    bool accessibility{false};
    // 可访问性设置只读查询（阶段8C：高对比、减少动画、字体缩放）。
    bool highContrast{false};
    bool reduceAnimation{false};
    float fontScale{1.0F};
    std::string adapterName{"unknown"};
};

class ApplicationHost {
  public:
    virtual ~ApplicationHost() = default;

    // 幂等初始化；失败返回 false（应用应安全退出或降级运行）。
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    [[nodiscard]] virtual core::AppLifecycle lifecycle() const = 0;

    // 归一化事件出队；队列空返回 false。多窗口事件按 WindowId 区分，
    // 事件先归一化再进入交互/焦点/状态/重绘流程（plan §2.3 不变量）。
    virtual bool pollEvent(core::HostEvent& out) = 0;

    virtual std::optional<core::WindowId> createWindow(
        const WindowDesc& desc) = 0;
    virtual void destroyWindow(core::WindowId id) = 0;
    [[nodiscard]] virtual std::optional<core::WindowMetrics> windowMetrics(
        core::WindowId id) const = 0;
    [[nodiscard]] virtual std::vector<core::WindowId> windowIds() const = 0;

    // 过渡适配：宿主托管的窗口以旧接口暴露（present/nativeSurface）。
    // fake host 无真实窗口，返回 nullptr。
    [[nodiscard]] virtual PlatformWindow* platformWindow(
        core::WindowId id) const = 0;

    // 平台服务（plan §3.1 PlatformServices）。不可用时返回 nullptr。
    [[nodiscard]] virtual Clipboard* clipboard() = 0;
    [[nodiscard]] virtual TextInputSession* textInputSession(
        core::WindowId id) = 0;
    [[nodiscard]] virtual PlatformCapabilities capabilities() const = 0;
};

// 阶段标识（阶段8A 契约冻结）。
[[nodiscard]] const char* hostStageName();

}  // namespace lumen::platform
