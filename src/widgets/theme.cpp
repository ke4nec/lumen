#include "lumen/widgets/theme.h"

#include <utility>

namespace lumen::widgets {
namespace {

// TextStyle 默认黑色视为“未设置”（与 painter 的 contentStyle 规则一致）。
bool styleHasColor(const core::TextStyle& style) {
    return !(style.color == core::Color{0, 0, 0, 255});
}

}  // namespace

Theme Theme::dark() {
    Theme theme;
    core::TextStyle title;
    title.fontSize = 20.0F;
    theme.titleStyle = title;
    theme.bodyStyle.fontSize = 14.0F;
    core::TextStyle caption;
    caption.fontSize = 12.0F;
    theme.captionStyle = caption;
    return theme;
}

Theme Theme::light() {
    Theme theme = dark();
    theme.pageBackground = core::Color{245, 245, 247, 255};
    theme.surface = core::Color{232, 232, 237, 255};
    theme.surfaceElevated = core::Color{252, 252, 253, 255};
    theme.text = core::Color{28, 28, 32, 255};
    theme.textMuted = core::Color{110, 110, 120, 255};
    return theme;
}

Theme Theme::fromSettings(
    const accessibility::AccessibilitySettings& settings, bool darkMode) {
    Theme theme = darkMode ? dark() : light();
    if (settings.fontScale != 1.0F && settings.fontScale > 0.0F) {
        theme.titleStyle.fontSize *= settings.fontScale;
        theme.bodyStyle.fontSize *= settings.fontScale;
        theme.captionStyle.fontSize *= settings.fontScale;
        theme.spacingUnit *= settings.fontScale;
    }
    if (settings.highContrast) {
        // 高对比：纯白/纯黑文本，主色加饱和。
        theme.text = darkMode ? core::Color{255, 255, 255, 255}
                              : core::Color{0, 0, 0, 255};
        theme.textMuted = theme.text;
        theme.primary = core::Color{120, 170, 255, 255};
    }
    return theme;
}

void applyTheme(core::Widget& root, const Theme& theme) {
    switch (root.type) {
        case core::WidgetType::Text:
        case core::WidgetType::Button:
        case core::WidgetType::TextField:
        case core::WidgetType::Checkbox:
        case core::WidgetType::Switch:
            if (!styleHasColor(root.textStyle)) {
                root.textStyle.color = theme.text;
            }
            break;
        case core::WidgetType::Container:
            if (root.color == core::Color::transparent()) {
                root.color = theme.surface;
            }
            break;
        default:
            break;
    }
    for (auto& child : root.children) {
        applyTheme(child, theme);
    }
}

core::Widget themedButton(std::string label, std::string onClick,
                          const Theme& theme, std::string key) {
    core::Widget button = core::makeButton(std::move(label), core::TextStyle{},
                                           core::EdgeInsets{}, 0.0F,
                                           std::move(key), std::nullopt,
                                           std::nullopt, std::move(onClick));
    button.textStyle.fontSize = theme.bodyStyle.fontSize;
    return button;
}

core::Widget themedTextField(std::string bind, std::string placeholder,
                             const Theme& theme, std::string key) {
    core::Widget field = core::makeTextField("", std::move(placeholder),
                                             core::TextStyle{},
                                             core::EdgeInsets{}, 0.0F,
                                             std::move(key));
    field.bind = std::move(bind);
    field.textStyle.fontSize = theme.bodyStyle.fontSize;
    return field;
}

core::Widget themedLabel(std::string text, const Theme& theme, bool muted) {
    core::Widget label = core::makeText(std::move(text));
    label.textStyle.fontSize = theme.bodyStyle.fontSize;
    label.textStyle.color = muted ? theme.textMuted : theme.text;
    return label;
}

core::Widget themedTitle(std::string text, const Theme& theme) {
    core::Widget title = core::makeText(std::move(text));
    title.textStyle.fontSize = theme.titleStyle.fontSize;
    title.textStyle.color = theme.text;
    return title;
}

core::Widget themedError(std::string text, const Theme& theme) {
    core::Widget error = core::makeText(std::move(text));
    error.textStyle.fontSize = theme.captionStyle.fontSize;
    error.textStyle.color = theme.error;
    error.semanticsRole = "text";
    return error;
}

}  // namespace lumen::widgets
