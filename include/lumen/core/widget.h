#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/core/icon_id.h"

namespace lumen::core {

enum class WidgetType {
    Container,
    Row,
    Column,
    Stack,
    Text,
    Button,
    TextField,
    // v0.3 阶段8D 应用基础组件（plan §3.4）。
    ScrollView,  // 垂直滚动视口：子内容主轴不限，painter 裁剪
    ListView,   // ScrollView + 稳定 key 列表语义（无虚拟化）
    Checkbox,   // 勾选框：bind 值 "true"/"false"，点击自动切换
    Switch,     // 开关：同 Checkbox 行为，不同绘制
    FocusScope, // 焦点域：Tab 遍历不越过边界（Dialog/Route 用）
    // M3（自用路线图）：约束布局扩展组件。
    Grid,        // 网格：固定列数/最小列宽自适应，行/列间距，窗口变化重排
    Image,       // 图像：已就绪 ImageId 绘制，未就绪固定占位（语义保留）
    VirtualList, // 虚拟列表：按需物化可见项（itemCount/itemBuilder/
                 // estimatedExtent/stable key/viewport cache）
    // M6（自用路线图）：视觉系统 V3 与控件库。
    Icon,        // 图标：矢量折线（语义 ID），装饰性
    Slider,      // 滑块：bind 数值（0..100），拖动/键盘改值
    ProgressBar, // 进度条：bind/value 0..100，无交互
    Radio,       // 单选：同 Checkbox 语义，圆形指示
    Tooltip,     // 提示气泡：hover 显示（常驻树，透明度切换）
    Dropdown,    // 下拉：当前值 + 展开选项（open 控制），选项点击经
                 // onClick("select:<index>") 交应用
    Tabs,        // 页签：bind 当前 tab id + 标签列表，点击切换
    ThemeScope,  // M6：局部主题域（子树覆盖父主题；布局期生效）
    // 集合控件（docs/lumen-collection-controls-design.md）：三控件共享
    // VirtualListSource 布局契约与 M3 虚拟化引擎；差异在 widgets 层控制器。
    List,        // 列表：一维行序列（选择/激活语义由 ListController 驱动）
    Tree,        // 树：层级模型扁平化为可见行序列（TreeController）
    TreeList,    // 树+列：Tree 能力 + 列系统与粘性表头（TreeListController）
};

enum class MainAxisAlignment {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class CrossAxisAlignment {
    Start,
    Center,
    End,
    Stretch,
};

enum class StackAlignment {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// 视觉系统（docs/lumen-visual-system-design.md §6.1）：控件语义声明。
// Widget 只描述语义/变体/尺寸/状态；最终颜色与几何由 StyleResolver 结
// 合 Theme 与交互状态解析，Widget 不被 Theme 应用过程原地修改。

// Button 外观变体。
enum class ButtonVariant : std::uint8_t {
    Filled,   // 主操作：accent 背景。
    Tonal,    // 次操作：低饱和容器。
    Outline,  // 边框按钮：透明背景 + 强边框。
    Ghost,    // 幽灵按钮：透明背景，hover/pressed 才出现容器色。
    Danger,   // 破坏性操作：错误色背景。
};

// 控件尺寸档位（Small/Compact、Medium/Comfortable、Large/Touch）；与
// Theme 的 ControlDensity 一起决定最小尺寸、内边距与圆角。
enum class ControlSize : std::uint8_t { Small, Medium, Large };

// 字段级局部样式覆盖：只在应用需要品牌定制时提供（§3.1）。空 = 全部
// 由 Theme 派生；显式设置的值（包括透明/黑色）按字面生效。
struct StyleOverrides {
    std::optional<Color> background{};
    std::optional<Color> foreground{};
    std::optional<Color> border{};
    // 边框线宽（>0 时 painter 双层绘制边框；容器卡片用，控件按 token）。
    std::optional<float> borderWidth{};
    std::optional<CornerRadius> radius{};
    std::optional<EdgeInsets> padding{};
    std::optional<TextStyle> text{};

    [[nodiscard]] bool empty() const {
        return !background.has_value() && !foreground.has_value() &&
               !border.has_value() && !borderWidth.has_value() &&
               !radius.has_value() && !padding.has_value() &&
               !text.has_value();
    }

    bool operator==(const StyleOverrides&) const = default;
};

struct Widget;  // 前置声明：VirtualListSource::buildItem 按值返回。

// M3（自用路线图）：VirtualList 数据源接口。应用拥有实现（通常由
// VirtualListController 适配），Widget 只存裸指针（UI 线程独占，生命周期
// 覆盖布局；operator== 比较指针身份）。布局期可回填实测 extent——实现
// 自持有可变缓存，修正后布局重新计算范围与位置。
class VirtualListSource {
  public:
    virtual ~VirtualListSource() = default;

    [[nodiscard]] virtual std::size_t itemCount() const = 0;
    // 项目初始估算高度（未测量项的占位）。
    [[nodiscard]] virtual float estimatedExtent() const = 0;
    // 项目 i 的高度：已测量取实测，否则估算。
    [[nodiscard]] virtual float extentOf(std::size_t index) const = 0;
    // 当前滚动偏移（应用侧 ScrollController 持有）。
    [[nodiscard]] virtual float scrollOffset() const = 0;
    // 内容总高（全部项 extentOf 之和；布局计算 scrollExtent）。
    [[nodiscard]] virtual float totalExtent() const = 0;
    // 项 i 顶部相对内容顶部的累计偏移（子项绝对定位）。
    [[nodiscard]] virtual float offsetOfIndex(std::size_t index) const = 0;
    // 可见项区间 [first, last)（含前后 cacheExtent 像素缓存）。
    [[nodiscard]] virtual std::pair<std::size_t, std::size_t> visibleRange(
        float viewportExtent, float cacheExtent) const = 0;
    // 布局用已夹取的偏移查询；支持 padding 使内容起点落在视口内部。
    [[nodiscard]] std::pair<std::size_t, std::size_t> visibleRangeAt(
        float offset, float viewportExtent, float cacheExtent) const;
    // 布局先同步视口，控制器据此更新滚动范围；静态数据源可忽略。
    virtual void updateViewport(float viewportExtent, float contentPadding) const {
        (void)viewportExtent;
        (void)contentPadding;
    }
    // 构建项目 i 的 Widget：必须携带含 index 的稳定 key（复用/焦点/
    // 语义身份依据；key 变化 = 项目替换）。
    [[nodiscard]] virtual Widget buildItem(std::size_t index) const = 0;
    // 布局期回填实测高度（幂等缓存写入，允许修正滚动锚点）。
    virtual void noteExtent(std::size_t index, float extent) const = 0;

    // --- 集合控件扩展（collection-controls-design §8.3） ---
    // TreeList 表头（非滚动 chrome）：返回空 Widget = 无表头。表头在
    // 行区上方固定，不随 scrollOffset 平移；高度由布局实测。
    // 默认实现（返回空 Widget）在 widget.cpp 中定义（此处 Widget 尚未
    // 完整定义）。
    [[nodiscard]] virtual Widget buildHeader() const;
    // 内容宽度回填（列宽分配用；幂等缓存写入，viewport 变化时重排）。
    virtual void noteContentWidth(float width) const { (void)width; }
};

// Immutable UI description. Aggregates are intentionally copyable so tests and
// the C++ DSL can build trees by value; runtime state lives in Element.
//
// Box model: `width`/`height` are border-box overrides (include `padding`,
// exclude `margin`) and are honored by every widget type, including the child
// constraints derived from them. `margin` is consumed by the parent when
// positioning; `padding` insets the content box.
//
// Container is single-child: only `children[0]` participates in layout; extra
// children are a programming error (asserted in debug builds).
struct Widget {
    WidgetType type{WidgetType::Container};
    std::string key{};

    // Box model shared by every widget.
    std::optional<float> width{};
    std::optional<float> height{};
    // Flex factor when this widget is a direct child of Row/Column.
    // 0 means inflexible; >0 participates in free-space distribution.
    float flex{0.0F};
    // Loose flex / content-sized ScrollView, bounded by the available viewport.
    bool shrinkWrap{false};
    bool alignContentStart{false}; // Button: left label, trailing icon column.
    bool reserveIconSpace{false};  // Button: keep that column when no icon is set.
    EdgeInsets padding{};
    EdgeInsets margin{};

    // Container styling.
    Color color{Color::transparent()};
    CornerRadius radius{CornerRadius::zero()};

    // Row/Column arrangement.
    MainAxisAlignment mainAxis{MainAxisAlignment::Start};
    CrossAxisAlignment crossAxis{CrossAxisAlignment::Start};
    float spacing{0.0F};

    // Stack arrangement.
    StackAlignment stackAlignment{StackAlignment::TopLeft};

    // Leaf content for Text/Button/TextField.
    std::string text{};
    TextStyle textStyle{};
    std::string placeholder{};

    // v0.3 阶段8B TextField 最小属性（plan §3.2 密码/只读/多行）。
    bool obscure{false};    // 密码模式：绘制为圆点
    bool readOnly{false};   // 只读：编辑键与 IME 提交被拒绝
    bool multiline{false};  // 多行：Enter 换行而非失焦

    // v0.3 阶段8C 语义覆盖（plan §3.3）：应用可覆盖 label/value/role/
    // actions；空字符串 = 使用 widget 默认值，semanticsActions 与推断值
    // 相或。role 取 SemanticsRole 名称（"button"/"checkbox"/...），由
    // lumen-accessibility 解析——core 不依赖语义枚举。
    std::string semanticsLabel{};
    std::string semanticsValue{};
    std::string semanticsRole{};
    std::uint32_t semanticsActions{0};

    // v0.3 阶段8D（plan §3.4）：
    // Checkbox/Switch 的选中状态（applyBinds 从 bind 值解析）。
    bool checked{false};
    // ScrollView/ListView/VirtualList 的当前滚动偏移（应用侧
    // ScrollController 持有，重建时写回；VirtualList 亦经 source 读取）。
    float scrollOffset{0.0F};

    // M3 Grid：固定列数（>0）或按最小列宽自适应（=0 时用
    // gridMinColumnWidth 推导 ≥1 列）；行列间距独立。
    int gridColumnCount{0};
    float gridMinColumnWidth{0.0F};
    float gridColumnGap{0.0F};
    float gridRowGap{0.0F};

    // M3 Image：imageId 为已上传资源的稳定 id（render::ImageId；
    // 0 = 未就绪，绘制固定占位）；imageSource 为资源路径/请求键
    //（诊断与语义保留，加载由应用侧 ResourceManager 驱动）。
    std::uint64_t imageId{0};
    std::string imageSource{};

    // M6 小字段（icon/transitionAlpha/showScrollbar/elevation/
    // dropdownOpen）集中声明于尾部 packed 区（消除中段 padding，
    // M7 性能门槛：Widget 体积直接影响 reconcile 构建/比较成本）。


    // M6 ThemeScope：局部主题指针（指向 style::Theme 拷贝，由
    // style::makeThemeScopeData 的 shared_ptr 保活；核心不接触样式类型，
    // 裸指针省去每节点拷贝的控件引用计数——M7 体积门槛）。
    const void* themeOverride{};

    // M3 VirtualList：数据源（应用拥有；空 = 空列表）与视口前后缓存
    //（像素）。children 必须为空——布局期按可见区物化。
    const VirtualListSource* virtualSource{nullptr};
    float virtualCacheExtent{200.0F};

    // 集合控件（collection-controls-design）：List/Tree/TreeList 复用
    // virtualSource 指针（源为 widgets 层控制器，实现 VirtualListSource）。
    // collectionSelectionMode 为声明值（0=None/1=Single/2=Multiple/
    // 3=Extended；真实选择状态在控制器，Widget 只承载语义声明）。
    std::uint8_t collectionSelectionMode{0};
    // TreeList 列向量指针（应用拥有的 std::vector<TreeListColumn>；
    // themeOverride 同模式）与表头显隐。
    const void* collectionColumns{nullptr};
    // 集合行标记：行 Button 由集合控制器构建；StyleResolver 把 selected
    // 折算为 color.selection.background（Tabs/Dropdown 的 selected 语义
    // 不受影响）。
    bool collectionRow{false};
    bool collectionShowHeader{false};

    // 视觉系统声明属性（visual-system-design §6.1）：enabled=false 时控
    // 件不可用（视觉、命中、键盘与语义一致拒绝）；invalid=true 表达校验
    // 失败（边框/辅助文本/语义）；selected 用于列表/工具栏选中语义。
    ButtonVariant buttonVariant{ButtonVariant::Filled};
    ControlSize controlSize{ControlSize::Medium};
    bool enabled{true};
    bool invalid{false};
    bool selected{false};
    // --- M6 集中区（小字段连续，消除 padding） ---
    bool showScrollbar{false};  // 滚动条显隐（滚动视口）
    IconId icon{IconId::None};  // 图标语义 ID（Icon 节点/Button 图标位）
    float elevation{0.0F};  // 层级（ElevationTokens；0 = 无阴影）
    float transitionAlpha{1.0F};  // 控件转场透明度（reduceAnimation 恒 1）
    StyleOverrides styleOverrides{};

    // Stage 3 semantics: `bind` names a StateStore key, `onClick` names a
    // handler in the app's HandlerRegistry. `bindPrefix` preserves the
    // literal part of a bound Text ("Count: " + value) across rebuilds.
    std::string bind{};
    std::string bindPrefix{};
    std::string onClick{};

    // Explicit position inside a Stack parent. When set, it overrides
    // stackAlignment for this child.
    std::optional<Offset> stackPosition{};

    std::vector<Widget> children{};

    // Structural equality over every field; DSL golden tests compare parsed
    // trees against C++-built ones.
    [[nodiscard]] bool operator==(const Widget& other) const = default;
};

// --- C++ declarative builders (Stage 1 subset, extended in Stage 3) ---

inline Widget makeContainer(Widget child,
                            std::optional<float> width = std::nullopt,
                            std::optional<float> height = std::nullopt,
                            EdgeInsets padding = {}, EdgeInsets margin = {},
                            Color color = Color::transparent(),
                            CornerRadius radius = CornerRadius::zero(),
                            std::string key = {}) {
    Widget widget;
    widget.type = WidgetType::Container;
    widget.width = width;
    widget.height = height;
    widget.padding = padding;
    widget.margin = margin;
    widget.color = color;
    widget.radius = radius;
    widget.key = std::move(key);
    widget.children.push_back(std::move(child));
    return widget;
}

inline Widget makeContainerLeaf(std::optional<float> width = std::nullopt,
                                std::optional<float> height = std::nullopt,
                                EdgeInsets padding = {}, EdgeInsets margin = {},
                                Color color = Color::transparent(),
                                std::string key = {}) {
    Widget widget;
    widget.type = WidgetType::Container;
    widget.width = width;
    widget.height = height;
    widget.padding = padding;
    widget.margin = margin;
    widget.color = color;
    widget.key = std::move(key);
    return widget;
}

inline Widget makeRow(std::vector<Widget> children,
                      MainAxisAlignment mainAxis = MainAxisAlignment::Start,
                      CrossAxisAlignment crossAxis = CrossAxisAlignment::Start,
                      float spacing = 0.0F, EdgeInsets padding = {},
                      EdgeInsets margin = {}, std::string key = {},
                      std::optional<float> width = std::nullopt,
                      std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Row;
    widget.mainAxis = mainAxis;
    widget.crossAxis = crossAxis;
    widget.spacing = spacing;
    widget.padding = padding;
    widget.margin = margin;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children = std::move(children);
    return widget;
}

inline Widget makeColumn(std::vector<Widget> children,
                         MainAxisAlignment mainAxis = MainAxisAlignment::Start,
                         CrossAxisAlignment crossAxis = CrossAxisAlignment::Start,
                         float spacing = 0.0F, EdgeInsets padding = {},
                         EdgeInsets margin = {}, std::string key = {},
                         std::optional<float> width = std::nullopt,
                         std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Column;
    widget.mainAxis = mainAxis;
    widget.crossAxis = crossAxis;
    widget.spacing = spacing;
    widget.padding = padding;
    widget.margin = margin;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children = std::move(children);
    return widget;
}

inline Widget makeStack(std::vector<Widget> children,
                        StackAlignment alignment = StackAlignment::TopLeft,
                        EdgeInsets padding = {}, EdgeInsets margin = {},
                        std::string key = {},
                        std::optional<float> width = std::nullopt,
                        std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Stack;
    widget.stackAlignment = alignment;
    widget.padding = padding;
    widget.margin = margin;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children = std::move(children);
    return widget;
}

inline Widget makeText(std::string content, TextStyle style = {},
                       EdgeInsets margin = {}, float flex = 0.0F,
                       std::string key = {},
                       std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt,
                       EdgeInsets padding = {}) {
    Widget widget;
    widget.type = WidgetType::Text;
    widget.text = std::move(content);
    widget.textStyle = style;
    widget.margin = margin;
    widget.padding = padding;
    widget.flex = flex;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    return widget;
}

inline Widget makeButton(std::string label, TextStyle style = {},
                         EdgeInsets margin = {}, float flex = 0.0F,
                         std::string key = {},
                         std::optional<float> width = std::nullopt,
                         std::optional<float> height = std::nullopt,
                         std::string onClick = {}) {
    Widget widget;
    widget.type = WidgetType::Button;
    widget.text = std::move(label);
    widget.textStyle = style;
    widget.margin = margin;
    widget.flex = flex;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.onClick = std::move(onClick);
    return widget;
}

inline Widget makeTextField(std::string value = {},
                            std::string placeholder = {}, TextStyle style = {},
                            EdgeInsets margin = {}, float flex = 0.0F,
                            std::string key = {},
                            std::optional<float> width = std::nullopt,
                            std::optional<float> height = std::nullopt,
                            std::string bind = {}) {
    Widget widget;
    widget.type = WidgetType::TextField;
    widget.text = std::move(value);
    widget.placeholder = std::move(placeholder);
    widget.textStyle = style;
    widget.margin = margin;
    widget.flex = flex;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.bind = std::move(bind);
    return widget;
}

// Marks a child as flexible inside Row/Column.
inline Widget withFlex(Widget child, float flex) {
    child.flex = flex;
    return child;
}

// Sets an explicit offset for a Stack child.
inline Widget withStackPosition(Widget child, Offset position) {
    child.stackPosition = position;
    return child;
}

// Attaches a HandlerRegistry event name; bubbles from the hit target upward.
inline Widget withOnClick(Widget child, std::string handler) {
    child.onClick = std::move(handler);
    return child;
}

// Attaches a StateStore key to any widget.
inline Widget withBind(Widget child, std::string key) {
    child.bind = std::move(key);
    return child;
}

// Attaches an identity key; used for reuse, focus tracking and press state.
inline Widget withKey(Widget child, std::string key) {
    child.key = std::move(key);
    return child;
}

// v0.3 阶段8B: TextField 属性修饰。
inline Widget withObscure(Widget child, bool obscure = true) {
    child.obscure = obscure;
    return child;
}
inline Widget withReadOnly(Widget child, bool readOnly = true) {
    child.readOnly = readOnly;
    return child;
}
inline Widget withMultiline(Widget child, bool multiline = true) {
    child.multiline = multiline;
    return child;
}

// 视觉系统语义修饰（visual-system-design §6.1）。控件 chrome 全部由
// Theme + StyleResolver 派生；这些修饰只表达语义声明与局部覆盖。
inline Widget withVariant(Widget child, ButtonVariant variant) {
    child.buttonVariant = variant;
    return child;
}
inline Widget withControlSize(Widget child, ControlSize size) {
    child.controlSize = size;
    return child;
}
inline Widget withEnabled(Widget child, bool enabled = true) {
    child.enabled = enabled;
    return child;
}
inline Widget withInvalid(Widget child, bool invalid = true) {
    child.invalid = invalid;
    return child;
}
inline Widget withSelected(Widget child, bool selected = true) {
    child.selected = selected;
    return child;
}
inline Widget withStyleOverrides(Widget child, StyleOverrides overrides) {
    child.styleOverrides = std::move(overrides);
    return child;
}

// v0.3 阶段8D: 滚动与选择组件构建器。
// ScrollView：单子内容（通常为 Column），纵向滚动；viewport 尺寸由
// width/height 或父约束给出，scrollOffset 由应用侧 ScrollController
// 在重建时写回。
inline Widget makeScrollView(Widget child, std::string key = {},
                             std::optional<float> width = std::nullopt,
                             std::optional<float> height = std::nullopt,
                             EdgeInsets padding = {}) {
    Widget widget;
    widget.type = WidgetType::ScrollView;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.padding = padding;
    widget.children.push_back(std::move(child));
    return widget;
}

// ListView：语义上的列表（role=list）；实现同 ScrollView，子节点应为带
// 稳定 key 的列表项（首期不做虚拟化，plan §3.4）。
inline Widget makeListView(Widget child, std::string key = {},
                           std::optional<float> width = std::nullopt,
                           std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::ListView;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children.push_back(std::move(child));
    return widget;
}

inline Widget withScrollOffset(Widget widget, float offset) {
    widget.scrollOffset = offset;
    return widget;
}

// Checkbox/Switch：bind 值 "true"/"false"（"1"/"0" 亦接受）；点击由框架
// 自动切换（与 TextField 编辑相同的内建行为）；label 用作语义标签。
inline Widget makeCheckbox(std::string label, std::string bind,
                           std::string key = {}, bool checked = false) {
    Widget widget;
    widget.type = WidgetType::Checkbox;
    widget.text = std::move(label);
    widget.bind = std::move(bind);
    widget.key = std::move(key);
    widget.checked = checked;
    return widget;
}

inline Widget makeSwitch(std::string label, std::string bind,
                         std::string key = {}, bool checked = false) {
    Widget widget;
    widget.type = WidgetType::Switch;
    widget.text = std::move(label);
    widget.bind = std::move(bind);
    widget.key = std::move(key);
    widget.checked = checked;
    return widget;
}

// FocusScope：单子容器；Tab 遍历在域内循环，不越过边界（modal
// barrier/Dialog 焦点恢复用）。
inline Widget makeFocusScope(Widget child, std::string key = {}) {
    Widget widget;
    widget.type = WidgetType::FocusScope;
    widget.key = std::move(key);
    widget.children.push_back(std::move(child));
    return widget;
}

// --- M6：图标与主题域 ---

// 图标节点：矢量图标（语义 ID；固定尺寸默认取 IconTheme.defaultSize，
// 可经 width/height 覆盖）。装饰性（无语义标签则不进语义树）。
inline Widget makeIcon(IconId icon, std::string key = {},
                       std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Icon;
    widget.icon = icon;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    return widget;
}

inline Widget withIcon(Widget child, IconId icon) {
    child.icon = icon;
    return child;
}

inline Widget withTransitionAlpha(Widget child, float alpha) {
    child.transitionAlpha = alpha;
    return child;
}

inline Widget withScrollbar(Widget child, bool show = true) {
    child.showScrollbar = show;
    return child;
}

// --- M6：控件库（Slider/ProgressBar/Radio/Tooltip/Dropdown/Tabs） ---

// Slider：拖动/键盘改值（bind 值为 0..100 整数串）。语义 role=slider。
inline Widget makeSlider(std::string bind, std::string key = {},
                         std::optional<float> width = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Slider;
    widget.bind = std::move(bind);
    widget.key = std::move(key);
    widget.width = width;
    return widget;
}

// ProgressBar：进度 0..100（value 或 bind）；无交互，语义 value。
inline Widget makeProgressBar(std::string value, std::string key = {},
                              std::optional<float> width = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::ProgressBar;
    widget.text = std::move(value);  // 值复用 text（bind 时 applyBinds 覆盖）
    widget.key = std::move(key);
    widget.width = width;
    return widget;
}

// Radio：bind "true"/"false"（组内互斥由应用写值实现；与 Checkbox 同
// 交互路径，圆形指示）。
inline Widget makeRadio(std::string label, std::string bind,
                        std::string key = {}, bool checked = false) {
    Widget widget;
    widget.type = WidgetType::Radio;
    widget.text = std::move(label);
    widget.bind = std::move(bind);
    widget.key = std::move(key);
    widget.checked = checked;
    return widget;
}

// Tooltip：提示气泡（文本；常驻树，应用控制显隐/定位，一般放 Stack）。
inline Widget makeTooltip(std::string text, std::string key = {}) {
    Widget widget;
    widget.type = WidgetType::Tooltip;
    widget.text = std::move(text);
    widget.key = std::move(key);
    return widget;
}

// Dropdown：当前值行（收起叶子）。M11 起选项经框架级 overlay 浮动菜单
//（widgets::DropdownController）呈现，不再作为 children 内嵌展开；点击
// 值行经 openHandler 打开菜单。值行自带 ChevronDown 图标。
inline Widget makeDropdown(std::string value, std::string openHandler,
                           std::string key = {},
                           std::optional<float> width = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Dropdown;
    widget.text = std::move(value);  // 当前值复用 text
    widget.onClick = std::move(openHandler);
    widget.buttonVariant = ButtonVariant::Outline;  // 值行 = 边框控件
    widget.key = std::move(key);
    widget.width = width;
    return widget;
}

// Tabs：标签行（children = 标签按钮，selected 标记当前；点击切换由
// 应用经 onClick 处理；bind 携带当前 tab id 供语义）。
inline Widget makeTabs(std::vector<Widget> tabButtons, std::string key = {},
                       std::optional<float> width = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Tabs;
    widget.key = std::move(key);
    widget.width = width;
    widget.children = std::move(tabButtons);
    return widget;
}

// M6：ThemeScope —— 子树主题覆盖（单子容器）。theme 为
// style::makeThemeScopeData 返回的 shared_ptr 的裸指针；调用方必须保持
// shared_ptr 存活（重建期间 UI 线程持有，如应用成员）。
inline Widget makeThemeScope(Widget child, const void* theme) {
    Widget widget;
    widget.type = WidgetType::ThemeScope;
    widget.themeOverride = theme;
    widget.children.push_back(std::move(child));
    return widget;
}

// --- M3（自用路线图）：Grid / Image / VirtualList ---

// Grid：子项按阅读顺序填入；固定列数（columnCount>0）或按最小列宽
// 自适应（minColumnWidth>0 时列数 = floor((可用宽+列间距)/
// (最小列宽+列间距))，至少 1 列）。窗口变化重排由约束传播自然发生。
inline Widget makeGrid(std::vector<Widget> children,
                       int columnCount = 0, float minColumnWidth = 0.0F,
                       float columnGap = 0.0F, float rowGap = 0.0F,
                       std::string key = {},
                       std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Grid;
    widget.gridColumnCount = columnCount;
    widget.gridMinColumnWidth = minColumnWidth;
    widget.gridColumnGap = columnGap;
    widget.gridRowGap = rowGap;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children = std::move(children);
    return widget;
}

// Image：声明式图像。imageId 为已上传资源（0 = 未就绪占位）；
// source 为资源路径（加载由应用侧 ResourceManager 异步驱动，就绪后
// 应用把 id 写回重建）。固定尺寸（width/height）推荐显式给定。
inline Widget makeImage(std::uint64_t imageId, std::string source,
                        std::optional<float> width = std::nullopt,
                        std::optional<float> height = std::nullopt,
                        std::string key = {}) {
    Widget widget;
    widget.type = WidgetType::Image;
    widget.imageId = imageId;
    widget.imageSource = std::move(source);
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    return widget;
}

// VirtualList：大数据量列表。不要求应用把全部子树放入 children——
// 布局期按 scrollOffset/estimatedExtent/实测 extent 计算可见区并经
// source 物化（含视口前后 cacheExtent 像素缓存）。项目稳定 key 由
// source 的 buildItem 提供；key 变化 = 项目替换（不携带旧状态）。
inline Widget makeVirtualList(const VirtualListSource* source,
                              std::string key = {},
                              std::optional<float> width = std::nullopt,
                              std::optional<float> height = std::nullopt,
                              float cacheExtent = 200.0F) {
    Widget widget;
    widget.type = WidgetType::VirtualList;
    widget.virtualSource = source;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.virtualCacheExtent = cacheExtent;
    return widget;
}

// --- 集合控件（collection-controls-design §6-8） ---

// List：一维行序列。source = ListController（widgets 层；实现
// VirtualListSource 并注入选择/激活语义）。布局直接复用 VirtualList 路径。
inline Widget makeList(const VirtualListSource* source,
                       std::string key = {},
                       std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt,
                       float cacheExtent = 200.0F) {
    Widget widget;
    widget.type = WidgetType::List;
    widget.virtualSource = source;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.virtualCacheExtent = cacheExtent;
    return widget;
}

// Tree：层级行序列。source = TreeController（可见节点扁平化）。
inline Widget makeTree(const VirtualListSource* source,
                       std::string key = {},
                       std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt,
                       float cacheExtent = 200.0F) {
    Widget widget;
    widget.type = WidgetType::Tree;
    widget.virtualSource = source;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.virtualCacheExtent = cacheExtent;
    return widget;
}

// TreeList：树 + 列。columns 为应用拥有的列向量指针（生命周期覆盖布局，
// themeOverride 同模式）；showHeader 控制粘性表头（source 的 buildHeader）。
inline Widget makeTreeList(const VirtualListSource* source,
                           const void* columns = nullptr,
                           bool showHeader = true,
                           std::string key = {},
                           std::optional<float> width = std::nullopt,
                           std::optional<float> height = std::nullopt,
                           float cacheExtent = 200.0F) {
    Widget widget;
    widget.type = WidgetType::TreeList;
    widget.virtualSource = source;
    widget.collectionColumns = columns;
    widget.collectionShowHeader = showHeader;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.virtualCacheExtent = cacheExtent;
    return widget;
}

// 集合选择模式声明（语义树与控制器一致性检查用；真实状态在控制器）。
inline Widget withSelectionMode(Widget widget, std::uint8_t mode) {
    widget.collectionSelectionMode = mode;
    return widget;
}

[[nodiscard]] bool isLeafWidget(WidgetType type);
[[nodiscard]] bool isFlexContainer(WidgetType type);
// v0.3 阶段8D：滚轮/键盘/语义滚动的命中目标类型。
[[nodiscard]] bool isScrollableWidget(WidgetType type);

}  // namespace lumen::core
