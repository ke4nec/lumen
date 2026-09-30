#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lumen::core {

// v0.3 阶段8B (plan §3.1 PlatformServices / §3.2 剪贴板): 交互层使用的
// 最小剪贴板契约，放在 core 以避免 core → platform 依赖。平台实现
// （platform::Clipboard）实现本接口；应用负责把宿主的服务接进来。
//
// 不可用（无桌面会话/权限被拒）时 setText 返回 false、text() 返回空；
// 编辑状态不得因此丢失。
//
// G-3（gap-backlog）MIME 数据层：格式枚举/原始字节读写/多格式原子放置
// 与变更事件（HostEventType::ClipboardChanged，docs/
// lumen-clipboard-service-design.md）。默认实现把 text/plain 映射到既有
// 纯文本虚函数、其余格式结构化降级（hasFormat=false / data 空 /
// setFormats 只写首个 text/plain）——纯文本宿主（Fake/旧实现）零改动，
// 新能力由平台实现按能力位如实报告（PlatformCapabilities）。
class ClipboardProvider {
  public:
    virtual ~ClipboardProvider() = default;
    [[nodiscard]] virtual bool hasText() const = 0;
    [[nodiscard]] virtual std::string text() const = 0;
    virtual bool setText(const std::string& value) = 0;
    virtual void clear() = 0;

    // --- G-3：MIME 数据层（默认实现 = 纯文本降级） ---
    // 常用 MIME 常量（约定小写；平台实现负责与系统格式互转）。
    static constexpr const char* kMimeText = "text/plain";
    static constexpr const char* kMimePng = "image/png";
    static constexpr const char* kMimeTsv = "text/tab-separated-values";

    // 当前剪贴板是否提供该格式（无内容/不可用 = false）。
    [[nodiscard]] virtual bool hasFormat(const std::string& mimeType) const {
        return mimeType == kMimeText && hasText();
    }
    // 读取该格式的原始字节（不可用返回空——与"空数据"不可区分，调用
    // 方先查 hasFormat）。
    [[nodiscard]] virtual std::vector<std::uint8_t> data(
        const std::string& mimeType) const {
        if (!hasFormat(mimeType)) {
            return {};
        }
        const std::string value = text();
        return {value.begin(), value.end()};
    }
    // 单格式放置（整体替换剪贴板内容；多格式同置用 setFormats）。
    virtual bool setData(const std::string& mimeType,
                         const std::vector<std::uint8_t>& bytes) {
        if (mimeType != "text/plain") {
            return false;
        }
        return setText(std::string(bytes.begin(), bytes.end()));
    }

    // 多格式条目（一次放置对外提供多种表示，如 DataGrid 选区 =
    // TSV + PNG；formats 只读视图见 setFormats 参数）。
    struct Entry {
        std::string mimeType{};
        std::vector<std::uint8_t> bytes{};
        Entry() = default;
        Entry(std::string mime, std::vector<std::uint8_t> data)
            : mimeType(std::move(mime)), bytes(std::move(data)) {}
    };
    // 原子多格式放置（平台支持时一次替换；默认实现降级为只写首个
    // text/plain 条目）。
    virtual bool setFormats(const std::vector<Entry>& entries) {
        for (const auto& entry : entries) {
            if (entry.mimeType == "text/plain") {
                return setText(
                    std::string(entry.bytes.begin(), entry.bytes.end()));
            }
        }
        return false;
    }
    // 当前可用格式列表（默认实现：有文本 = {"text/plain"}，否则空）。
    [[nodiscard]] virtual std::vector<std::string> formats() const {
        if (hasText()) {
            return {"text/plain"};
        }
        return {};
    }

    // 便捷层：image/png 可用性（粘贴按钮可用态刷新等）。
    [[nodiscard]] virtual bool hasImage() const {
        return hasFormat(kMimePng);
    }
};

}  // namespace lumen::core
