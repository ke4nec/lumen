#pragma once

// G-4（gap-backlog）：对话框便利层。
//
// 「确认覆盖文件？」三行代码的事——showMessage/showConfirm/showPrompt
// 封装既有 makeDialog 手拼模式（settings 示例同款）：模态 barrier + 焦点
// 域 + Escape/关闭统一规则 + 焦点恢复 + M10 对话框转场 + 语义契约
//（docs/lumen-dialog-host-design.md）。
//
// 应用装配点（四钩子，全部一次性接线）：
//   - build：`if (auto dialog = dialogs_.build(shell)) ui = stack(ui, *dialog);`
//   - config.onKey：`if (dialogs_.handleKey(shell, key, mods, ch)) return true;`
//     （modal 优先于路由返回规则——plan §3.4）
//   - config.onCloseRequested：`if (dialogs_.handleCloseRequested(shell))
//     return true;`
//   - config.onRebuilt：`dialogs_.onRebuilt(shell);`（焦点安置/恢复）
//
// 行为契约与手拼 Dialog 一致（同契约测试锁定）：barrier 点击 = 取消；
// Escape = 取消；窗口关闭请求 = 取消（消费不退出）；确认/取消按钮经
// HandlerRegistry（键盘 Enter/Space 同路径）；prompt 的 TextField 绑定
// 独立命名空间（多次/嵌套互不串值）。同一时间至多一个便利对话框
//（busy() 拒绝新请求——嵌套对话框走应用自拼）。

#include <functional>
#include <optional>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"

namespace lumen::widgets {

class DialogHost {
  public:
    // 按钮文案（默认 OK/Cancel）。注：嵌套类带 NSDMI 时不能用作外围类
    // 的默认实参（GCC 完备性规则）——显式默认构造保留默认文案。
    struct Buttons {
        std::string ok{};
        std::string cancel{};
        Buttons(std::string okLabel = "OK",
                std::string cancelLabel = "Cancel")
            : ok(std::move(okLabel)), cancel(std::move(cancelLabel)) {}
    };

    // 消息对话框（单按钮；barrier/Escape = 关闭，均触发 onDismiss）。
    void showMessage(app::AppShell& shell, std::string title,
                     std::string body, Buttons buttons = {},
                     std::function<void()> onDismiss = {});
    // 确认对话框（ok=true / cancel 与 barrier、Escape=false）。
    void showConfirm(app::AppShell& shell, std::string title,
                     std::string body, Buttons buttons = {},
                     std::function<void(bool accepted)> onResult = {});
    // 输入对话框（ok=文本；cancel/barrier/Escape=nullopt）。
    void showPrompt(app::AppShell& shell, std::string title,
                    std::string body, std::string initial = {},
                    Buttons buttons = {},
                    std::function<void(std::optional<std::string>)>
                        onResult = {});

    // --- 装配点（见文件头注释） ---
    [[nodiscard]] std::optional<core::Widget> build(
        const app::AppShell& shell) const;
    // Escape 路由（仅对话框打开时消费；settings 同款统一规则）。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers modifiers = core::kModifierNone,
                   char keyChar = 0);
    // 窗口关闭请求：打开时取消对话框并消费（不退出应用）。
    bool handleCloseRequested(app::AppShell& shell);
    // 重建后焦点安置：对话框打开且焦点不在域内 → 进对话框（prompt 聚焦
    // 字段）；关闭后 → 恢复唤起前焦点。
    void onRebuilt(app::AppShell& shell);

    [[nodiscard]] bool busy() const { return kind_ != Kind::None; }
    // prompt 对话框当前文本（测试/语义查询；其他种类为空）。
    [[nodiscard]] std::string promptText(const app::AppShell& shell) const;

  private:
    enum class Kind : std::uint8_t { None, Message, Confirm, Prompt };

    void show(app::AppShell& shell, Kind kind, std::string title,
              std::string body, std::string initial, Buttons buttons);
    void accept(app::AppShell& shell);
    void cancel(app::AppShell& shell);
    void finish(app::AppShell& shell);
    void registerHandlers(app::AppShell& shell);
    [[nodiscard]] std::string dialogKey() const { return "dialog-host"; }
    [[nodiscard]] std::string promptBind() const {
        return "dialog-host:prompt";
    }

    Kind kind_{Kind::None};
    bool closing_{false};
    std::string title_{};
    std::string body_{};
    Buttons buttons_{};
    std::string initial_{};
    std::string returnFocusKey_{};
    bool focusSettlePending_{false};
    // 回调在 finish 前拷贝触发（finish 清状态后再回调，避免回调内再开
    // 对话框被 busy() 拒绝）。
    std::function<void()> onDismiss_{};
    std::function<void(bool)> onConfirm_{};
    std::function<void(std::optional<std::string>)> onPrompt_{};
};

}  // namespace lumen::widgets
