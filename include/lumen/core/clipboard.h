#pragma once

#include <string>

namespace lumen::core {

// v0.3 阶段8B (plan §3.1 PlatformServices / §3.2 剪贴板): 交互层使用的
// 最小剪贴板契约，放在 core 以避免 core → platform 依赖。平台实现
// （platform::Clipboard）实现本接口；应用负责把宿主的服务接进来。
//
// 不可用（无桌面会话/权限被拒）时 setText 返回 false、text() 返回空；
// 编辑状态不得因此丢失。
class ClipboardProvider {
  public:
    virtual ~ClipboardProvider() = default;
    [[nodiscard]] virtual bool hasText() const = 0;
    [[nodiscard]] virtual std::string text() const = 0;
    virtual bool setText(const std::string& value) = 0;
    virtual void clear() = 0;
};

}  // namespace lumen::core
