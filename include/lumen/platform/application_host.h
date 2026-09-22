#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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
    // 原生软件呈现表面（无 GPU/SDL renderer）：GPU 回退窗口用，避免
    // 回退路径依赖渲染器重建（M2：counter_software_present_failure
    // 奇偶性依赖 SDL_UpdateWindowSurface 路径）。
    bool softwarePresentation{false};
    // 自定义标题栏（lumen-titlebar-design §4）：无边框窗口（系统标题
    // 栏/边框移除），应用自绘 caption 与窗口控制。runApp 据此向宿主注册
    // 拖拽区谓词；宿主 hit-test 另提供 resize 边（拖动/双击最大化/snap
    // 等行为由平台原生路径提供）。
    bool customTitleBar{false};
    // 透明窗口（design/gallery.html 圆角主界面）：SDL_WINDOW_TRANSPARENT
    // + runApp 将 CPU 清屏色转全透明——应用内容自绘圆角（根/标题栏容器
    // CornerRadius），圆角外像素按 alpha 交桌面合成器。Windows DWM 支持
    // 良好；无合成器的 X11 会话角落退化为黑（自用范围可接受）。外部渲
    // 染器（Skia/GPU）的透明 clear 由后端装配负责。
    bool transparent{false};
};

// 剪贴板服务（实现 core::ClipboardProvider，交互层直接消费）。不可用
// （无桌面会话/权限被拒）时 setText 返回 false、text() 返回空；应用状
// 态不得因此丢失（plan §2.3 不变量）。
class Clipboard : public core::ClipboardProvider {
  public:
    ~Clipboard() override = default;
};

// --- M4（自用路线图）：平台服务契约 ---
//
// 服务失败必须结构化（类别 + 可读原因）且不阻塞 UI 线程；能力先行
// 查询（PlatformCapabilities），调用方按可用性降级。

enum class ServiceError : std::uint8_t {
    None = 0,
    Unavailable,  // 服务在当前平台/构建不可用
    Cancelled,    // 用户取消（如文件对话框）
    Failed,       // 平台调用失败
};

struct ServiceResult {
    bool ok{false};
    ServiceError error{ServiceError::None};
    // 用户可读诊断（不可用原因/失败详情）；成功时为空。
    std::string message{};

    [[nodiscard]] static ServiceResult success() {
        return ServiceResult{true, ServiceError::None, {}};
    }
    [[nodiscard]] static ServiceResult unavailable(std::string why) {
        return ServiceResult{false, ServiceError::Unavailable, std::move(why)};
    }
    [[nodiscard]] static ServiceResult cancelled() {
        return ServiceResult{false, ServiceError::Cancelled,
                             "cancelled by user"};
    }
    [[nodiscard]] static ServiceResult failed(std::string why) {
        return ServiceResult{false, ServiceError::Failed, std::move(why)};
    }

    [[nodiscard]] bool operator==(const ServiceResult&) const = default;
};

// 文件选择请求：打开（forSave=false）或保存（forSave=true）。
struct FileDialogRequest {
    std::string title{};
    // 过滤器（如 "*.txt"、"Images"；平台解释，空 = 不过滤）。
    std::vector<std::string> filters{};
    // 保存对话框默认名 / 打开对话框起始目录。
    std::string defaultName{};
    bool forSave{false};
    bool allowMultiple{false};
};

struct FileDialogResult {
    ServiceResult status{};
    // 用户选择的路径（allowMultiple 时可多条；取消为空）。
    std::vector<std::string> paths{};

    [[nodiscard]] bool operator==(const FileDialogResult&) const = default;
};

// 通知（桌面系统通知；不可用时结构化降级，不得阻塞）。
struct NotificationRequest {
    std::string title{};
    std::string body{};
};

// 鼠标系统光标形状（SDL 为进程级；窗口参数保留给窗口级后端）。
enum class SystemCursor : std::uint8_t {
    Arrow,
    IBeam,
    Wait,
    Crosshair,
    PointingHand,
    Grab,
    Grabbing,
    ResizeAll,
    ResizeNS,
    ResizeEW,
    Forbidden,
};

// 窗口图标（straight RGBA8）。
struct WindowIcon {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba{};
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
    // SDL 宿主从各桌面原生偏好读取；查询不可用时返回安全默认值，
    // 应用仍可通过 AppShell::setAccessibilitySettings 覆盖。
    bool highContrast{false};
    bool reduceAnimation{false};
    float fontScale{1.0F};
    std::string adapterName{"unknown"};
    // --- M4：外观与服务统一报告 ---
    // 外观（系统主题输入；不可用时安全默认）。
    bool prefersDarkMode{false};
    core::Color accentColor{core::Color::fromRGBA(63, 81, 181)};
    // 服务可用性（调用前查询；不可用服务返回结构化失败）。
    bool fileDialogs{false};
    bool notifications{false};
    bool openUrl{false};
    bool cursorShape{false};
    bool windowIcon{false};
    // 至少一项 OS 偏好已查询成功；个别未暴露的字段保留安全默认值。
    bool systemAccessibilityPreferences{false};
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

    // 空闲等待：最多阻塞 timeoutMs 或直到新事件到达（事件泵唤醒）。
    // 默认 no-op（Fake host/测试：立即返回，纯轮询）；SDL 宿主用
    // SDL_WaitEventTimeout 实现。不得在该方法内分发事件。
    virtual void waitForEvents(std::uint32_t timeoutMs) {
        (void)timeoutMs;
    }

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

    // --- M4：平台服务（结构化失败，不阻塞 UI 线程） ---
    // 默认实现全部 Unavailable（契约 host/未支持平台安全降级）。

    // 外部链接（同步打开系统浏览器；结果立即返回）。
    [[nodiscard]] virtual ServiceResult openUrl(const std::string& url);
    // 文件选择（异步：请求立即返回，完成经 pollEvent 以
    // FileDialogCompleted 事件交付；取消 = 空路径且无错误）。
    [[nodiscard]] virtual ServiceResult requestFileDialog(
        core::WindowId id, const FileDialogRequest& request);
    // 系统通知（不可用平台返回 Unavailable 结构化降级）。
    [[nodiscard]] virtual ServiceResult postNotification(
        const NotificationRequest& request);
    // 鼠标系统光标形状。
    virtual void setCursor(core::WindowId id, SystemCursor cursor);
    // 窗口图标（straight RGBA8）。
    [[nodiscard]] virtual ServiceResult setWindowIcon(
        core::WindowId id, const WindowIcon& icon);

    // --- 自定义标题栏（lumen-titlebar-design §4.3）：窗口操作与拖拽区 ---
    // 默认实现安全 no-op（契约 host/未支持平台结构化降级）；无效窗口 id
    // 时单窗口宿主可挂靠首个窗口（requestFileDialog 先例）。
    virtual void minimizeWindow(core::WindowId id);
    // 最大化/还原切换；结果经 WindowMaximized/WindowRestored 事件交付。
    virtual void toggleMaximizeWindow(core::WindowId id);
    // 请求关闭：与系统 X 同路径——宿主合成 WindowCloseRequested 事件经
    // pollEvent 交付（runApp → shell.requestClose，应用可消费）。
    virtual void requestWindowClose(core::WindowId id);
    // 窗口拖拽区谓词（窗口逻辑坐标；true = 该点可拖动移窗）。宿主
    // hit-test 先判 resize 边、再咨询谓词；空谓词/未注册 = 无拖拽区。
    virtual void setWindowDragRegion(
        core::WindowId id, std::function<bool(core::Offset)> predicate);

    // --- M13：原生无障碍桥装配 ---
    // 原生窗口句柄（PlatformAccessibilityHost.nativeWindow 语义；契约
    // host/无原生层返回 nullptr）。SDL 宿主：Windows = HWND、macOS =
    // NSWindow*、Linux = nullptr（AT-SPI 走会话总线）。
    [[nodiscard]] virtual void* nativeWindowHandle(core::WindowId id) const {
        (void)id;
        return nullptr;
    }
    // 原生语义桥激活状态（PlatformCapabilities.accessibility 如实上报；
    // 默认 no-op）。
    virtual void noteAccessibilityBridgeActive(bool active) {
        (void)active;
    }
};

// 阶段标识（阶段8A 契约冻结）。
[[nodiscard]] const char* hostStageName();

}  // namespace lumen::platform
