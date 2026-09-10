#include "lumen/platform/platform_window.h"

namespace lumen::platform {

const char* platformStageName() {
    return "stage2";
}

PresentResult PlatformWindow::present(const render::PixelBuffer& buffer) {
    // 默认呈现：假实现丢弃缓冲区（未接窗口系统）。
    if (buffer.width <= 0 || buffer.height <= 0) {
        return PresentResult::Rejected;
    }
    return PresentResult::Ok;
}

}  // namespace lumen::platform
