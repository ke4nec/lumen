#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/render/renderer.h"

namespace lumen::render {

// v0.2 阶段7B (plan §3.1): 值类型绘制命令。Painter 只负责把 RenderNode
// 转换为命令；Renderer 负责验证、回放和提交。CPU、Skia 光栅和 Skia GPU
// 消费同一份命令。

// 预留 2x3 仿射变换（平移/旋转/缩放）。v0.2 后端必须接受并忽略非单位
// 变换之外的字段；命令语义按逻辑坐标定义。
struct Transform2D {
    float a{1.0F};
    float b{0.0F};
    float c{0.0F};
    float d{1.0F};
    float tx{0.0F};
    float ty{0.0F};

    [[nodiscard]] constexpr bool operator==(const Transform2D&) const = default;
    [[nodiscard]] static constexpr Transform2D identity() { return {}; }
};

enum class CommandType : std::uint8_t {
    Save,
    Restore,
    ClipRect,
    DrawRect,
    DrawText,
    DrawImage,
    // 阶段7D 资源生命周期：UI 线程通过命令提交 upload/unload，GPU 对象只在
    // Renderer 所属线程创建和销毁（plan §3.3）。
    UploadImage,
    UnloadImage,
    // M6（自用路线图）：视觉系统 V3 扩展。
    DrawIcon,    // 矢量图标（归一化折线组，stroke）
    DrawShadow,  // 阴影（Skia blur；CPU 后端以 token 边框降级）
};

// 单条绘制命令。字段并集避免堆分配 variant；`bounds` 是命令影响的逻辑
// 区域（painter 按节点矩形填充），damage 裁剪据此丢弃不可能改变目标
// 区域的绘制命令。`alpha`/`transform` 为 v0.2 预留字段。
struct RenderCommand {
    CommandType type{CommandType::Save};
    // ClipRect/DrawRect/DrawImage 目标区域（逻辑坐标）。
    core::Rect rect{};
    core::Color color{};
    core::CornerRadius radius{};
    TextRun textRun{};
    core::TextStyle textStyle{};
    ImageId image{0};
    // UploadImage 携带的不可变 CPU 像素（straight RGBA）。
    PixelBuffer pixels{};
    float alpha{1.0F};
    Transform2D transform{Transform2D::identity()};
    core::Rect bounds{};
    bool hasBounds{false};
    // M6：DrawIcon —— 归一化折线组（0..1 盒内坐标）+ 线宽；颜色复用
    // color，盒子复用 rect。DrawShadow —— 偏移/模糊半径复用 rect.origin
    //（offset）/ rect.size.width（blur）；颜色复用 color。
    std::vector<std::vector<core::Offset>> polylines{};
    float strokeWidth{1.5F};

    [[nodiscard]] bool operator==(const RenderCommand&) const = default;
};

// 一帧的命令序列。builder 方法与 Renderer 的即时路径一一对应，所以
// 同一个 walker 模板既能驱动 Renderer 也能录制命令。
class RenderCommandList {
  public:
    void reserve(std::size_t count) { commands_.reserve(count); }

    void save() { push(CommandType::Save); }
    void restore() { push(CommandType::Restore); }

    void clipRect(core::Rect rect) {
        RenderCommand& command = push(CommandType::ClipRect);
        command.rect = rect;
        command.bounds = rect;
        command.hasBounds = true;
    }

    void drawRect(core::Rect rect, core::Color color,
                  core::CornerRadius radius = {}) {
        RenderCommand& command = push(CommandType::DrawRect);
        command.rect = rect;
        command.color = color;
        command.radius = radius;
        command.bounds = rect;
        command.hasBounds = true;
    }

    void drawText(TextRun run, core::TextStyle style,
                  std::optional<core::Rect> bounds = std::nullopt) {
        RenderCommand& command = push(CommandType::DrawText);
        command.textRun = std::move(run);
        command.textStyle = style;
        if (bounds.has_value()) {
            command.bounds = *bounds;
            command.hasBounds = true;
        }
    }

    void drawImage(ImageId id, core::Rect destination) {
        RenderCommand& command = push(CommandType::DrawImage);
        command.image = id;
        command.rect = destination;
        command.bounds = destination;
        command.hasBounds = true;
    }

    void drawIcon(std::vector<std::vector<core::Offset>> polylines,
                  core::Rect box, core::Color color, float strokeWidth,
                  std::optional<core::Rect> bounds = std::nullopt) {
        RenderCommand& command = push(CommandType::DrawIcon);
        command.polylines = std::move(polylines);
        command.rect = box;
        command.color = color;
        command.strokeWidth = strokeWidth;
        if (bounds.has_value()) {
            command.bounds = *bounds;
            command.hasBounds = true;
        }
    }

    void drawShadow(core::Rect elevatedBox, core::Color color,
                    core::Offset offset, float blur,
                    std::optional<core::Rect> bounds = std::nullopt) {
        RenderCommand& command = push(CommandType::DrawShadow);
        command.rect = elevatedBox;
        command.color = color;
        // 偏移/模糊经专用字段（见 drawShadow 扩展字段）。
        command.transform.tx = offset.x;
        command.transform.ty = offset.y;
        command.strokeWidth = blur;
        if (bounds.has_value()) {
            command.bounds = *bounds;
            command.hasBounds = true;
        }
    }

    void uploadImage(ImageId id, PixelBuffer pixels) {
        RenderCommand& command = push(CommandType::UploadImage);
        command.image = id;
        command.pixels = std::move(pixels);
    }

    void unloadImage(ImageId id) {
        RenderCommand& command = push(CommandType::UnloadImage);
        command.image = id;
    }

    [[nodiscard]] const std::vector<RenderCommand>& commands() const {
        return commands_;
    }
    // 反序列化与裁剪工具的逐条追加入口；录制请走上面的 builder。
    void append(RenderCommand command) {
        commands_.push_back(std::move(command));
    }
    [[nodiscard]] bool empty() const { return commands_.empty(); }
    [[nodiscard]] std::size_t size() const { return commands_.size(); }
    void clear() { commands_.clear(); }

    // 绘制命令数（不含 Save/Restore/ClipRect/Upload/Unload）。
    [[nodiscard]] std::size_t drawCount() const {
        std::size_t count = 0;
        for (const auto& command : commands_) {
            switch (command.type) {
                case CommandType::DrawRect:
                case CommandType::DrawText:
                case CommandType::DrawImage:
                case CommandType::DrawIcon:
                case CommandType::DrawShadow:
                    ++count;
                    break;
                default:
                    break;
            }
        }
        return count;
    }

    [[nodiscard]] bool operator==(const RenderCommandList&) const = default;

  private:
    RenderCommand& push(CommandType type) {
        commands_.emplace_back();
        commands_.back().type = type;
        return commands_.back();
    }

    std::vector<RenderCommand> commands_{};
};

// 一帧命令集的工具：序列化与 damage 裁剪。FrameInfo / RendererCapabilities
// / RenderStats / RenderSurfaceDesc 定义在 renderer.h（Renderer 直接持有）。

// 二进制序列化（小端）。blob 以 magic+version 开头；反序列化校验失败返回
// false 并且不修改 out。
[[nodiscard]] std::string serializeCommands(const RenderCommandList& list);
[[nodiscard]] bool deserializeCommands(const std::string& blob,
                                       RenderCommandList& out);

// 丢弃与 bounds 不相交的绘制命令（保留 Save/Restore/ClipRect 结构与
// Upload/Unload），用于 damage 范围回放。无 bounds 的绘制命令保守保留。
[[nodiscard]] RenderCommandList cullCommandsOutside(const RenderCommandList& list,
                                                    core::Rect bounds);

// 所有带 bounds 的绘制命令的并集；用于判断局部回放是否可行。
[[nodiscard]] std::optional<core::Rect> commandDrawBounds(const RenderCommandList& list);

}  // namespace lumen::render
