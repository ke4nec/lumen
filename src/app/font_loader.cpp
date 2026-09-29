#include "lumen/app/font_loader.h"

namespace lumen::app {

FontLoader::FontLoader(Factory factory) {
    // 工厂在后台线程执行；结果（含失败诊断）经互斥锁交付，UI 线程
    // pump 一次取走。工厂抛异常视作失败并记录，不崩线程。
    worker_ = std::thread([this, factory = std::move(factory)]() {
        std::shared_ptr<text::FontManager> fonts;
        std::string diagnostics;
        try {
            fonts = factory(&diagnostics);
        } catch (const std::exception& error) {
            diagnostics = std::string("font factory threw: ") + error.what();
        } catch (...) {
            diagnostics = "font factory threw an unknown exception";
        }
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = std::move(fonts);
        diagnostics_ = std::move(diagnostics);
        done_ = true;
    });
}

FontLoader::~FontLoader() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool FontLoader::finished() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return done_;
}

std::shared_ptr<text::FontManager> FontLoader::pump(
    std::string* diagnostics) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!done_) {
        return nullptr;
    }
    if (diagnostics != nullptr) {
        *diagnostics = diagnostics_;
    }
    return std::move(result_);
}

}  // namespace lumen::app
