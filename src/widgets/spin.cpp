// Spin 数值步进（docs/lumen-spin-design.md）。组合件实现——外框 Row
// 承载 textfield token 边框/背景（focused/invalid 反应由控制器按焦点与
// store 文本即时折算），field 边框抑制为 0；stepper 为两个半高 Ghost
// 图标按钮（显式宽高覆盖 min 尺寸，layout"border-box 优先"invariant）。

#include "lumen/widgets/spin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "lumen/accessibility/semantics.h"

namespace lumen::widgets {
namespace {

// stepper 集群尺度（design §9.1；随密度三档）。
constexpr float kStepperWidth[3] = {24.0F, 28.0F, 32.0F};
constexpr float kStepperIcon[3] = {12.0F, 14.0F, 16.0F};

// ControlSize → 档位索引（resolver::sizeIndexFor 同规则；本编译单元
// 不引入 resolver 内部符号，故本地推导）。
std::uint8_t sizeIndexFor(const style::Metrics& metrics,
                          core::ControlSize size) {
    const int offset = static_cast<int>(size) - 1;
    const int index = static_cast<int>(metrics.baseIndex) + offset;
    return static_cast<std::uint8_t>(std::clamp(index, 0, 2));
}

// ASCII 数字解析（design §2：不做本地化分组/小数点变体）。可解析：
// 可选符号 + 至少一位数字 + 至多一个小数点，整串消费。
std::optional<double> parseDouble(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::size_t i = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    bool digits = false;
    bool dot = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            digits = true;
        } else if (c == '.' && !dot) {
            dot = true;
        } else {
            return std::nullopt;
        }
    }
    if (!digits) {
        return std::nullopt;
    }
    try {
        return std::stod(text);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

SpinController::SpinController(double initialValue, std::string key)
    : key_{std::move(key)} {
    setValue(initialValue);
}

std::string SpinController::fieldKey() const { return key_ + ":field"; }
std::string SpinController::bindKey() const { return key_ + ":value"; }
std::string SpinController::upKey() const { return "spin:up:" + key_; }
std::string SpinController::downKey() const { return "spin:down:" + key_; }

void SpinController::setRange(double min, double max) {
    min_ = std::min(min, max);
    max_ = std::max(min, max);
    setValue(value_);
}

void SpinController::setStep(double step) {
    step_ = step > 0.0 ? step : 1.0;
}

void SpinController::setPageStep(double step) {
    pageStep_ = step > 0.0 ? step : step_;
}

void SpinController::setDecimals(int digits) {
    decimals_ = std::clamp(digits, 0, 6);
    setValue(value_);
}

void SpinController::setWrap(bool wrap) { wrap_ = wrap; }

void SpinController::setControlSize(core::ControlSize size) {
    controlSize_ = size;
}

void SpinController::setLabel(std::string label) { label_ = std::move(label); }

void SpinController::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

double SpinController::roundToDecimals(double v) const {
    const double scale = std::pow(10.0, decimals_);
    return std::round(v * scale) / scale;
}

double SpinController::clampOrWrap(double v) const {
    // 直接设值一律钳制（GTK 口径：程序化设值不环绕）；wrap 只作用于
    // 步进通道（stepBy 的越界相邻跳转）。
    return std::clamp(v, min_, max_);
}

void SpinController::syncStore() {
    if (shell_ == nullptr) {
        return;
    }
    const std::string formatted = formatValue();
    if (shell_->state().get(bindKey()) != formatted) {
        // store 写入经 AppShell bind 订阅触发重建（值变化即重绘）。
        shell_->state().set(bindKey(), formatted);
    }
}

void SpinController::setValue(double v) {
    const double snapped =
        min_ + std::round((clampOrWrap(v) - min_) / step_) * step_;
    const double next = roundToDecimals(clampOrWrap(snapped));
    if (next == value_ && shell_ != nullptr &&
        shell_->state().get(bindKey()) == formatValue()) {
        return;
    }
    const bool changed = next != value_;
    value_ = next;
    syncStore();
    if (changed && onValueChanged) {
        onValueChanged(value_);
    }
}

bool SpinController::atMin() const { return value_ <= min_ + 1e-9; }

bool SpinController::atMax() const { return value_ >= max_ - 1e-9; }

std::string SpinController::formatValue() const {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals_, value_);
    return buffer;
}

std::optional<double> SpinController::currentTextValue() const {
    if (shell_ == nullptr) {
        return std::nullopt;
    }
    return parseDouble(shell_->state().get(bindKey()));
}

void SpinController::stepBy(double delta) {
    // 编辑中的合法文本优先为基数（键入候选优先——GTK 惯例）；wrap 时
    // 越界相邻跳转（min 之下 → max、max 之上 → min，design §5.2）。
    const double base = currentTextValue().value_or(value_);
    double v = base + delta;
    if (wrap_) {
        if (v < min_) {
            v = max_;
        } else if (v > max_) {
            v = min_;
        }
    }
    setValue(v);
}

void SpinController::stepUp() { stepBy(step_); }

void SpinController::stepDown() { stepBy(-step_); }

bool SpinController::commitText(const std::string& text) {
    const auto parsed = parseDouble(text);
    if (!parsed.has_value()) {
        return false;
    }
    setValue(*parsed);
    if (onCommitted) {
        onCommitted(value_);
    }
    return true;
}

void SpinController::revert() { syncStore(); }

bool SpinController::editingInvalid() const { return !currentTextValue().has_value(); }

void SpinController::attach(app::AppShell& shell) {
    shell_ = &shell;
    if (!shell.state().has(bindKey())) {
        shell.state().set(bindKey(), formatValue());
    }
    // stepper 单击：按住期间已由 step() 步进则吞并（tapStep 清标志）。
    shell.handlers()[upKey()] = [this] { tapStep(+1); };
    shell.handlers()[downKey()] = [this] { tapStep(-1); };
    attached_ = true;
}

void SpinController::tapStep(int direction) {
    if (holdStepped_) {
        holdStepped_ = false;
        return;
    }
    direction > 0 ? stepUp() : stepDown();
    focusFieldFromStepper();
}

void SpinController::focusFieldFromStepper() {
    // 指针点 stepper 会先清掉 field 焦点（interaction 非字段命中即失焦），
    // 按稿件 field.focus() 口径收拢回来——外框保持 focused 蓝边。键盘已在
    // ▲/▼ 上时不动（整控件共用 focused 边框，Tab 顺序不断）。
    if (shell_ == nullptr || !enabled_) {
        return;
    }
    const std::string focusedKey = shell_->focus().focusedKey();
    if (focusedKey == upKey() || focusedKey == downKey() ||
        focusedKey == fieldKey()) {
        return;
    }
    if (const auto* node = core::findNodeByKey(shell_->root(), fieldKey())) {
        shell_->controller().focusNode(*node);
        // 焦点切换改变外框边框（focused 蓝边）与 caret，需重建；值未变时
        // setValue 无脏标记，此处补（到界点击顶住无事件也应蓝边）。
        shell_->markDirty();
    }
}

core::Widget SpinController::build(const style::Theme& theme) {
    const std::uint8_t index = sizeIndexFor(theme.metrics, controlSize_);
    const float height = theme.metrics.minHeight[index];
    const float stepperWidth = kStepperWidth[index];
    const float iconSize = kStepperIcon[index];
    // 边框盒：子内容内缩 1px 边框宽绘制（CSS border-box 口径）——不透明的
    // 分隔线/hover 填充不再盖住外框描边；中缝 1px 取整拆分（floor/ceil），
    // 使横向分隔线落在整数像素上（半像素 19.5 会虚成两行 50% 灰）。
    const float borderW = theme.metrics.controlBorderWidth;
    const float contentH = height - 2.0F * borderW;
    const float upH = std::floor((contentH - 1.0F) / 2.0F);
    const float downH = contentH - 1.0F - upH;

    // 整控件共用 focused 蓝边：field caret、Tab 到 ▲/▼、按住 stepper 任一
    // 即蓝（稿件 field.focus() 口径：点 stepper 也要蓝）。失焦提交按整控件
    // 口径（Spin 内 Tab 不提交，离开整控件才 ≡ Enter）。
    const std::string focusedKey =
        shell_ != nullptr ? shell_->focus().focusedKey() : std::string{};
    const std::string pressedKey =
        shell_ != nullptr ? shell_->controller().pressedKey() : std::string{};
    const bool stepperFocused =
        focusedKey == upKey() || focusedKey == downKey();
    const bool stepperPressed =
        pressedKey == upKey() || pressedKey == downKey();
    const bool focused =
        enabled_ && (focusedKey == fieldKey() || stepperFocused ||
                     stepperPressed);
    if (wasFocused_ && !focused && shell_ != nullptr) {
        const std::string text = shell_->state().get(bindKey());
        if (!commitText(text)) {
            revert();
        }
    }
    wasFocused_ = focused;

    const bool invalid = editingInvalid();

    // field：bind 驱动文本；边框/背景抑制（外框由行承载）。disabled 时
    // 不抑制背景——resolver 的 disabledBackground 需要生效（§9.3）。
    // shrinkWrap：外框按内容宽收拢（稿件 design/spin.html
    // .spin{width:max-content;min-width:148}，§9.1 最小宽 120/148/176），
    // flex 预算只在容器窄于内容时压缩 field。缺此标记时 field 会吞掉整条
    // 行的剩余宽，Spin 作为非 flex 子节点放进 Row/Column 即撑破父容器。
    core::Widget field = core::makeTextField(
        formatValue(), "", core::TextStyle{}, core::EdgeInsets{},
        /*flex=*/1.0F, fieldKey(), std::nullopt, std::nullopt, bindKey());
    field.shrinkWrap = true;
    // 档位随控件（§9.1 尺度表：field 最小宽 96/120/144、水平内边距
    // 8/12/16）——不设则 Small 档样本（Overview Spin 瓦片）按 Comfortable
    // 折算，最小宽与内边距都落到 Medium 值。
    field.controlSize = controlSize_;
    field.styleOverrides.borderWidth = 0.0F;
    if (enabled_) {
        field.styleOverrides.background = core::Color::transparent();
    }
    field.enabled = enabled_;
    if (invalid) {
        field.invalid = true;  // 语义 invalid 同步（视觉边框在行上）
    }

    // stepper 集群：半高 chrome 图标按钮（design §9.2/§9.3：hover 表面
    // 派生 + 前景提亮、pressed = List pressed；显式宽高覆盖 icon-only
    // 方形 min）。到界（atBound）：chevron 淡化 + hover/pressed 背景抑制
    //（稿件 .at-bound:hover{background:transparent}——顶住无位移，不给
    // 误导性高亮；仍可命中）。
    // 角部直角（稿件 .spin-step 无 border-radius，§5"外缘右侧圆角随
    // controlRadius，内缘直角"）：填充贴满半格，圆角由行的 clipRounded
    // 裁出——按钮自带 controlRadius 时 hover/pressed 会缩成悬浮药丸。
    auto makeStepButton = [&](core::IconId icon, const std::string& k,
                              float buttonHeight, bool atBound) {
        core::Widget button =
            core::makeButton("", core::TextStyle{}, core::EdgeInsets{},
                             0.0F, k, stepperWidth, buttonHeight, k);
        button.buttonVariant = core::ButtonVariant::Chrome;
        button.controlSize = controlSize_;
        button.icon = icon;
        button.enabled = enabled_;
        button.styleOverrides.iconSize = iconSize;
        button.styleOverrides.radius = core::CornerRadius::zero();
        if (atBound) {
            // overrides 在状态折算后应用：前景淡化压住 hover/pressed 提亮，
            // 背景透明压住 hover/pressed 填充（稿件到界无高亮口径）。
            button.styleOverrides.foreground = theme.colors.disabledContent;
            button.styleOverrides.background = core::Color::transparent();
        }
        return button;
    };
    core::Widget up = makeStepButton(core::IconId::ChevronUp, upKey(), upH,
                                     atMax());
    // field 与 stepper 的 1px 分隔线（design §9.1 cluster 左缘）：高取内容
    // 高（边框内），与外框描边丁字相接不再盖边。
    core::Widget fieldSep = core::makeContainerLeaf(
        1.0F, contentH, core::EdgeInsets{}, core::EdgeInsets{},
        theme.colors.borderDefault, key_ + ":field-sep");
    core::Widget mid = core::makeContainerLeaf(
        stepperWidth, 1.0F, core::EdgeInsets{}, core::EdgeInsets{},
        theme.colors.borderDefault, key_ + ":stepper-sep");
    core::Widget down = makeStepButton(core::IconId::ChevronDown, downKey(),
                                       downH, atMin());
    core::Widget cluster =
        core::makeColumn({std::move(up), std::move(mid), std::move(down)},
                         core::MainAxisAlignment::Start,
                         core::CrossAxisAlignment::Stretch, 0.0F,
                         core::EdgeInsets{}, core::EdgeInsets{},
                         key_ + ":stepper", stepperWidth, std::nullopt);

    // 行 = 外框（textfield token；focused/invalid 反应即时折算）。padding
    // 内缩边框宽：子内容画在描边之内（border-box），hover/分隔线不盖边。
    core::Widget row = core::makeRow(
        {std::move(field), std::move(fieldSep), std::move(cluster)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
        0.0F, core::EdgeInsets::all(borderW), core::EdgeInsets{}, key_,
        std::nullopt, height);
    row.color = theme.textField.background;
    row.radius = core::CornerRadius::all(theme.metrics.controlRadius[index]);
    row.styleOverrides.borderWidth = theme.metrics.controlBorderWidth;
    row.styleOverrides.border =
        invalid ? theme.textField.borderInvalid
                : focused ? theme.textField.borderFocused
                          : theme.textField.border;
    // 稿件 .spin{overflow:hidden}：贴角的 stepper 填充按外框圆角门控
    //（visual-system §11.1 第一防线），方形 hover/pressed 不再盖过右缘角部。
    row.clipRounded = true;

    // 语义（design §7）：spinbutton role + value 文本；编辑经 field 的
    // TextField 语义（SetValue 通道既有）。Increase/Decrease 专用 action
    // 枚举留 M13 provider 落地（设计稿 §7 已列开放项）。
    row.semanticsRole = "spinButton";
    row.semanticsLabel = label_;
    row.semanticsValue = formatValue();
    row.semanticsActions =
        accessibility::kActionFocus | accessibility::kActionSetValue;
    return row;
}

bool SpinController::handleKey(app::AppShell& shell, core::Key key,
                               core::KeyModifiers mods, char keyChar) {
    (void)mods;
    (void)keyChar;
    if (!attached_ || !enabled_) {
        return false;
    }
    // 三停靠点口径：步进键在 field 与 ▲/▼ 上都生效（Tab 到 stepper 后方向
    // 键仍可调值，焦点不动）；Enter/Escape 只在 field 上（▲/▼ 上的 Enter
    // 走按钮激活即 tapStep，不与提交混叠）。
    const std::string focusedKey = shell.focus().focusedKey();
    const bool onField = focusedKey == fieldKey();
    const bool onStepper = focusedKey == upKey() || focusedKey == downKey();
    if (!onField && !onStepper) {
        return false;
    }
    switch (key) {
        case core::Key::Up:
            stepUp();
            return true;
        case core::Key::Down:
            stepDown();
            return true;
        case core::Key::PageUp:
            stepBy(pageStep_);
            return true;
        case core::Key::PageDown:
            stepBy(-pageStep_);
            return true;
        case core::Key::Home:
            setValue(min_);
            return true;
        case core::Key::End:
            setValue(max_);
            return true;
        case core::Key::Enter: {
            if (!onField) {
                return false;  // stepper 上的 Enter 交按钮激活（tapStep）
            }
            const std::string text = shell.state().get(bindKey());
            if (!commitText(text)) {
                // 非法：保持 invalid、不提交（design §6.2）。
            }
            return true;
        }
        case core::Key::Escape: {
            if (!onField) {
                return false;
            }
            // 有可恢复内容（文本偏离或非法）才消费；否则交回应用
            //（Escape 的返回/关闭导航不被字段抢占——design §6.2）。
            const std::string text = shell.state().get(bindKey());
            if (text != formatValue() || editingInvalid()) {
                revert();
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

bool SpinController::handleWheel(app::AppShell& shell, core::Offset position,
                                 core::Offset delta) {
    if (!enabled_) {
        return false;
    }
    const auto* node = core::findNodeByKey(shell.root(), key_);
    if (node == nullptr) {
        return false;
    }
    const core::Rect rect{core::absoluteOffset(shell.root(), key_),
                          node->size};
    if (position.x < rect.origin.x || position.y < rect.origin.y ||
        position.x > rect.origin.x + rect.size.width ||
        position.y > rect.origin.y + rect.size.height) {
        return false;
    }
    // |dy| 阈值滤触摸板微抖（滚动语义同档一拍）；正 dy（向下滚）= 减。
    if (std::abs(delta.y) < 20.0F) {
        return false;
    }
    delta.y < 0.0F ? stepUp() : stepDown();
    return true;
}

bool SpinController::step(app::AppShell& shell, std::uint64_t nowMs) {
    const std::string& pressed = shell.controller().pressedKey();
    int direction = 0;
    if (pressed == upKey()) {
        direction = +1;
    } else if (pressed == downKey()) {
        direction = -1;
    } else {
        // 释放/取消：清理按住状态。已知边缘：按住已步进后 pointerCancel
        //（无单击）且后续无 tick——holdStepped_ 残留会吞并下一次单击的
        // 一步；下一次 step()/单击自行恢复。
        holdActive_ = false;
        holdStepped_ = false;
        return false;
    }
    if (!enabled_) {
        holdActive_ = false;
        holdStepped_ = false;
        return false;
    }
    if (shell.controller().isDragging()) {
        // 按住后移出按钮命中区（超过拖动 slop）：重复停止、按住状态
        // 复位（design §6.1"移出命中区即停"）；释放时无单击（拖动释放
        // 不触发 handler），tapStep 不会被调用。
        holdActive_ = false;
        holdStepped_ = false;
        return false;
    }
    if (!holdActive_) {
        // 首拍：立即步进（tap 由单击 handler 兜底，两路互斥经
        // holdStepped_），随后 500ms 延迟进入重复节奏。按住即把焦点收拢
        // 到 field（tapStep 同口径），外框保持 focused 蓝边。
        holdActive_ = true;
        holdStepped_ = true;
        holdNextMs_ = nowMs + shell.theme().motion.spinRepeatDelayMs;
        stepBy(direction > 0 ? step_ : -step_);
        focusFieldFromStepper();
        return true;
    }
    if (nowMs >= holdNextMs_) {
        stepBy(direction > 0 ? step_ : -step_);
        focusFieldFromStepper();
        holdNextMs_ = std::max<std::uint64_t>(
            holdNextMs_ + shell.theme().motion.spinRepeatIntervalMs, nowMs);
    }
    return true;
}

}  // namespace lumen::widgets
