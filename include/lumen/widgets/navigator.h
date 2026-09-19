#pragma once

#include <optional>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/core/state.h"
#include "lumen/core/widget.h"
#include "lumen/style/theme.h"

namespace lumen::widgets {

// v0.3 阶段8D (plan §3.4): Navigator/Route。
//
// 应用持有一个 NavigatorController：push/pop 维护命名路由栈；
// handleBack 统一处理 Escape/返回键/窗口关闭请求（modal 优先，plan
// §3.4 返回键/Escape/焦点恢复的统一规则），返回是否已消费。焦点恢复：
// pop 后应用重新聚焦路由内的第一个可聚焦节点（与 FocusScope 配合）。
class NavigatorController {
  public:
    explicit NavigatorController(std::string initialRoute);

    void push(const std::string& route);
    // 弹出顶层路由；根路由不可弹出（返回 false）。
    bool pop();
    void popToRoot();
    [[nodiscard]] const std::string& current() const;
    [[nodiscard]] const std::vector<std::string>& stack() const;
    // 统一返回处理：Escape/平台返回键/窗口关闭请求。
    // modalOpen=true 时先请求关闭 modal（返回 true = 已消费）。
    [[nodiscard]] bool handleBack(bool modalOpen = false);

  private:
    std::vector<std::string> stack_{};
};

// Dialog 构建：modal barrier + FocusScope 内容卡（plan §3.4 overlay、
// modal barrier、Escape 与焦点恢复）。barrier 点击触发 onDismiss
// handler；内容包裹 FocusScope 使 Tab 遍历不逃出 dialog。视觉全部来自
// Theme 的 DialogTokens（visual-system §7.4）。scrimRadius：透明窗口应
// 用传入窗口圆角——scrim 全窗矩形会把压暗色涂进圆角外的透明像素。
[[nodiscard]] core::Widget makeDialog(core::Widget content,
                                      const style::Theme& theme,
                                      std::string onDismiss,
                                      std::string key = {},
                                      core::Size windowSize = core::Size{
                                          800.0F, 600.0F},
                                      float scrimRadius = 0.0F);

// Body scrolls within the available height; actions remain outside its clip.
// The caller owns bodyScrollOffset, routed from the <key>-body-scroll viewport.
[[nodiscard]] core::Widget makeDialog(core::Widget body, core::Widget actions,
                                      const style::Theme& theme,
                                      std::string onDismiss, std::string key,
                                      core::Size windowSize,
                                      float bodyScrollOffset = 0.0F,
                                      float scrimRadius = 0.0F);

}  // namespace lumen::widgets
