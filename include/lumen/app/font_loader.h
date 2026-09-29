#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "lumen/text/font_manager.h"

namespace lumen::app {

// M14-C：字体异步生命周期。RunOptions.fontFactory 通常在首帧前同步执行
// （fontconfig/CoreText/GDI 枚举阻塞 UI 线程）；FontLoader 把工厂移到
// 后台线程——应用首帧以占位度量渲染，完成后经 pump() 在 UI 线程交付一次，
// runApp 据此热替换 AppShell 字体并请求资源帧。失败保持占位并保留诊断
// 字符串，不阻塞启动（M1 出口条件）。
//
// 线程契约：Factory 只在后台线程调用一次；pump()/finished() 只在 UI
// 线程调用。析构 join 工作线程并丢弃未交付的结果（取消语义）——工厂
// 必须在有限时间内返回。
class FontLoader {
  public:
    // 与 createSystemFontManager/createSkiaFontManager 同形：诊断经出参
    // 返回（成功与失败都填写），空返回值 = 失败/保持占位。
    using Factory = std::function<
        std::shared_ptr<text::FontManager>(std::string* diagnostics)>;

    explicit FontLoader(Factory factory);
    ~FontLoader();
    FontLoader(const FontLoader&) = delete;
    FontLoader& operator=(const FontLoader&) = delete;

    // 工厂是否已返回（成功或失败）。
    [[nodiscard]] bool finished() const;

    // 完成后一次性取走结果；重复调用返回空。diagnostics 可为空指针。
    std::shared_ptr<text::FontManager> pump(std::string* diagnostics);

  private:
    mutable std::mutex mutex_{};
    std::shared_ptr<text::FontManager> result_{};
    std::string diagnostics_{};
    bool done_{false};
    std::thread worker_{};
};

}  // namespace lumen::app
