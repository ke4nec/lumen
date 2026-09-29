// M14-D：DataGrid 契约测试（docs/lumen-datagrid-design.md §8/§16/§17）。
//
// 覆盖首版契约切片：列模型与列宽调整、行虚拟化物化、选择（共享
// SelectionModel）、键盘导航与列焦点、排序回调与状态、TSV 复制粘贴、
// 单元格编辑与校验。2026-09-28 第二批：真实指针 Ctrl/Shift 修饰键路径、
// 单元格点击定位列、提交失败拦截（切格/切行/排序）、编辑器程序化焦点
// 与编辑态 Enter 提交（IME composing 除外）、排序升→降→清除循环、列
// 显隐/顺序/minWidth、复选框选择列、双击进入编辑。
// 2026-09-28 第三批：双轴几何（横向视口 + 表头/数据同源平移 + 滚轮
// 分量路由）、内容窄于视口的铺满收敛、列宽手柄（拖动/钳制/双击复位/
// 键盘步进/Tab 停靠）、调宽先提交编辑。第四批：多列排序（Shift 追加/
// 循环/移除/普通点击收敛/指针透传/列集收缩清键）、当前格焦点环（统一
// 格式盒 + identity 稳定回归）、表头复选框三态（indeterminate + 语义
// value="mixed"）。
// 水平虚拟化/RTL/拖放为后续增量（不在本文件断言）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/style.h"
#include "lumen/core/widget.h"
#include "lumen/core/clipboard.h"
#include "lumen/widgets/datagrid.h"

using namespace lumen;
using widgets::DataColumn;
using widgets::DataGridController;
using widgets::SelectionMode;

namespace {

// 确定性剪贴板（core::ClipboardProvider 实现；runApp 注入宿主剪贴板的
// 测试替身）。
class FakeClipboard final : public core::ClipboardProvider {
  public:
    bool hasText() const override { return !text_.empty(); }
    std::string text() const override { return text_; }
    bool setText(const std::string& value) override {
        text_ = value;
        return true;
    }
    void clear() override { text_.clear(); }

  private:
    std::string text_{};
};

// 10 行 × 3 列的小型数据集（name/qty/editable）。
struct GridFixture {
    app::AppShell shell;
    DataGridController grid;
    FakeClipboard clipboard;
    std::vector<std::string> sortedBy{};
    std::vector<std::vector<std::string>> pasted{};
    std::vector<std::string> edited{};
    std::vector<std::string> activated{};

    GridFixture() : shell(makeConfig(&grid)), grid() {
        shell.controller().setClipboard(&clipboard);
        grid.attach(shell, "grid");
        grid.setColumns({
            DataColumn{"name", "Name", 100.0F, true, true, false},
            DataColumn{"qty", "Qty", 80.0F, true, false, true},
            DataColumn{"note", "Note", 140.0F, false, false, true},
        });
        grid.setRowCount(10);
        grid.setCellText([](std::size_t row, const std::string& col) {
            if (col == "name") return "item-" + std::to_string(row);
            if (col == "qty") return std::to_string((row + 1) * 3);
            return "note " + std::to_string(row);
        });
        grid.onSortRequest = [this](const std::string& col, bool ascending) {
            sortedBy.push_back(col.empty()
                                   ? std::string("clear")
                                   : col + (ascending ? "+" : "-"));
        };
        grid.onRowsPasted = [this](const std::vector<std::vector<std::string>>& rows) {
            pasted = rows;
        };
        grid.onCellEdited = [this](std::size_t row, const std::string& col,
                                   const std::string& text) {
            edited.push_back(std::to_string(row) + ":" + col + ":" + text);
        };
        grid.onRowActivated = [this](const std::string& key) {
            activated.push_back(key);
        };
    }

    static app::ShellConfig makeConfig(DataGridController* grid) {
        app::ShellConfig config;
        config.initialView = core::Size{400.0F, 300.0F};
        // build 引用成员地址：lambda 在 shell 构造完成后才会执行。
        config.build = [grid] { return grid->build(); };
        return config;
    }

    void render() { (void)shell.renderFrame(); }

    // 真实指针路径（P0）：按节点 key 中心点击；modifiers 经
    // pointerDown 传入（集合行/单元格 Extended 选择语义）。
    void click(const char* nodeKey,
               core::KeyModifiers modifiers = core::kModifierNone) {
        const core::RenderNode* node = core::findNodeByKey(shell.root(), nodeKey);
        REQUIRE(node != nullptr);
        const core::Offset at =
            core::absoluteOffset(shell.root(), nodeKey) +
            core::Offset{node->size.width * 0.5F, node->size.height * 0.5F};
        shell.pointerDown(at, modifiers);
        shell.pointerUp(at);
    }
};

}  // namespace

TEST_CASE("datagrid_column_model_and_resize", "[widgets][datagrid]") {
    GridFixture fx;
    CHECK(fx.grid.columns().size() == 3);
    // 低于下限的宽度被钳制（kMinColumnWidth=40）。
    CHECK(fx.grid.resizeColumn("qty", 8.0F));
    auto widths = fx.grid.columnWidths();
    REQUIRE(widths.size() == 3);
    CHECK(widths[1].second == 40.0F);
    // resizable=false 的列拒绝调整。
    CHECK_FALSE(fx.grid.resizeColumn("note", 200.0F));
    // 未知列拒绝。
    CHECK_FALSE(fx.grid.resizeColumn("nope", 100.0F));
}

TEST_CASE("datagrid_virtualizes_rows_and_builds_cells", "[widgets][datagrid]") {
    GridFixture fx;
    // 100 行：可见 + cacheExtent（200px）内只物化窗口（10 行会全部落在
    // 缓存区内，观察不到虚拟化）。
    fx.grid.setRowCount(100);
    auto root = fx.grid.build();
    fx.shell.markDirty();
    fx.render();
    // 首屏物化 < 总行数（虚拟化；视口 300 高 / 行高 40）。
    std::size_t rows = 0;
    std::size_t texts = 0;
    std::size_t checkboxes = 0;
    const std::function<void(const core::RenderNode&)> walk =
        [&](const core::RenderNode& node) {
            if (node.key.starts_with("grid:item:")) ++rows;
            if (node.type == core::WidgetType::Text) ++texts;
            if (node.key.starts_with("grid:check-cell:")) ++checkboxes;
            for (const auto& child : node.children) walk(child);
        };
    walk(fx.shell.root());
    CHECK(rows < 100);
    CHECK(rows >= 5);
    // 每物化行 3 个单元格文本；表头 = 2 个静态文本 + 1 个可排序 Ghost
    // 按钮（name）+ 表头复选框格（Extended 默认模式）。
    CHECK(texts >= rows * 3 + 2);
    CHECK(checkboxes == rows);
    // 表头存在。
    const auto* header = core::findNodeByKey(fx.shell.root(), "grid:header");
    REQUIRE(header != nullptr);
    // 单元格契约（§12/§16/§18）：统一格式盒（Container）内嵌内容——
    // 固定列宽 + 水平内边距 12 + 单行省略；当前格环经盒边框承载。
    const core::Widget row0 = fx.grid.buildItem(0);
    REQUIRE(row0.children.size() == 1);
    const core::Widget& content = row0.children.front();
    // [复选框格, name, qty, note]。
    REQUIRE(content.children.size() == 4);
    CHECK(content.children.front().width.value_or(0.0F) == 44.0F);
    const core::Widget& nameCell = content.children[1];
    REQUIRE(nameCell.type == core::WidgetType::Container);
    CHECK(nameCell.width.value_or(0.0F) == 100.0F);
    REQUIRE(nameCell.children.size() == 1);
    const core::Widget& nameText = nameCell.children.front();
    CHECK(nameText.padding.left == 12.0F);
    CHECK(nameText.textStyle.overflow == core::TextOverflow::Ellipsis);
    CHECK(nameText.textStyle.maxLines == 1);
    CHECK(nameText.onClick == "grid:grid:cell:r0:name");
}

TEST_CASE("datagrid_selection_follows_clicks_and_keyboard", "[widgets][datagrid]") {
    GridFixture fx;
    fx.grid.setSelectionMode(SelectionMode::Extended);
    auto root = fx.grid.build();
    fx.shell.markDirty();
    fx.render();

    fx.grid.setCurrentKey("r2", false);
    CHECK(fx.grid.selection().currentKey() == "r2");
    CHECK(fx.grid.selection().isSelected("r2"));  // Extended 随动
    // 键盘导航：Down 移动 current 并随动选择。
    CHECK(fx.grid.handleKey(core::Key::Down, core::kModifierNone));
    CHECK(fx.grid.selection().currentKey() == "r3");
    // Shift+Down 扩展区间选择（Extended 随动 = 替换语义：当前选择
    // {r3}，扩展后为 {r3, r4}）。
    CHECK(fx.grid.handleKey(core::Key::Down, core::kModifierShift));
    CHECK(fx.grid.selection().isSelected("r3"));
    CHECK(fx.grid.selection().isSelected("r4"));
    CHECK(fx.grid.selection().selectedCount() == 2);
    // 列焦点移动。
    CHECK(fx.grid.currentColumn() == "name");
    CHECK(fx.grid.handleKey(core::Key::Right, core::kModifierNone));
    CHECK(fx.grid.currentColumn() == "qty");
    CHECK(fx.grid.handleKey(core::Key::Left, core::kModifierNone));
    CHECK(fx.grid.currentColumn() == "name");
}

TEST_CASE("datagrid_sort_request_cycles_state_and_callback", "[widgets][datagrid]") {
    GridFixture fx;
    fx.grid.requestSort("name");
    CHECK(fx.grid.sortColumn() == "name");
    CHECK(fx.grid.sortAscending());
    REQUIRE(fx.sortedBy.size() == 1);
    CHECK(fx.sortedBy.front() == "name+");
    fx.grid.requestSort("name");
    CHECK_FALSE(fx.grid.sortAscending());
    CHECK(fx.sortedBy.back() == "name-");
    // 第三次点击清除排序（§11.2 升 → 降 → 清除；回调空列 key）。
    fx.grid.requestSort("name");
    CHECK(fx.grid.sortColumn().empty());
    CHECK(fx.grid.sortAscending());
    REQUIRE(fx.sortedBy.size() == 3);
    CHECK(fx.sortedBy.back() == "clear");
    // 不可排序列拒绝。
    fx.grid.requestSort("note");
    CHECK(fx.grid.sortColumn().empty());  // 未变
    CHECK(fx.sortedBy.size() == 3);
    // 清除后再次点击从升序开始。
    fx.grid.requestSort("name");
    CHECK(fx.grid.sortColumn() == "name");
    CHECK(fx.sortedBy.back() == "name+");
}

TEST_CASE("datagrid_copy_paste_tsv", "[widgets][datagrid]") {
    GridFixture fx;
    auto root = fx.grid.build();
    fx.shell.markDirty();
    fx.render();
    fx.grid.selection().setSelected({"r1", "r3"});
    CHECK(fx.grid.copySelection() == 2);
    CHECK(fx.clipboard.text() ==
          "item-1\t6\tnote 1\nitem-3\t12\tnote 3");
    // 粘贴分发（应用侧应用数据不在本测试职责内）。
    fx.clipboard.setText("pasted-a\tpasted-b\npasted-c\t\tpasted-d");
    CHECK(fx.grid.pasteRows());
    REQUIRE(fx.pasted.size() == 2);
    CHECK(fx.pasted[0][0] == "pasted-a");
    CHECK(fx.pasted[1].size() == 3);
    CHECK(fx.pasted[1][2] == "pasted-d");
    // Ctrl+C/Ctrl+V 键盘路径（回调为整体替换语义：剪贴板此时为单行
    // TSV，pasted 被替换为 1 行）。
    fx.grid.selection().setSelected({"r0"});
    CHECK(fx.grid.handleKey(core::Key::None, core::kModifierCtrl, 'c'));
    CHECK(fx.grid.handleKey(core::Key::None, core::kModifierCtrl, 'v'));
    REQUIRE(fx.pasted.size() == 1);
    CHECK(fx.pasted[0][0] == "item-0");
}

TEST_CASE("datagrid_cell_editing_with_validation", "[widgets][datagrid]") {
    GridFixture fx;
    auto root = fx.grid.build();
    fx.shell.markDirty();
    fx.render();
    fx.grid.setCurrentColumn("qty");
    fx.grid.setCellValidator("qty", [](const std::string& text) {
        return text.find_first_not_of("0123456789") == std::string::npos
                   ? ""
                   : "digits only";
    });

    // 只读列拒绝编辑。
    CHECK_FALSE(fx.grid.beginEdit(0, "name"));
    CHECK(fx.grid.beginEdit(0, "qty"));
    CHECK(fx.grid.editing());
    // 初值来自 cellText。
    CHECK(fx.shell.state().get("grid:edit") == "3");

    // 校验失败：保留编辑态 + 错误文案。
    fx.shell.state().set("grid:edit", "abc");
    CHECK_FALSE(fx.grid.commitEdit());
    CHECK(fx.grid.editing());
    CHECK(fx.grid.editError() == "digits only");
    CHECK(fx.edited.empty());

    // 校验通过：提交回调 + 退出编辑。
    fx.shell.state().set("grid:edit", "42");
    CHECK(fx.grid.commitEdit());
    CHECK_FALSE(fx.grid.editing());
    REQUIRE(fx.edited.size() == 1);
    CHECK(fx.edited.front() == "0:qty:42");

    // Escape 取消。
    fx.grid.beginEdit(1, "qty");
    fx.shell.state().set("grid:edit", "zz");
    CHECK(fx.grid.handleKey(core::Key::Escape, core::kModifierNone));
    CHECK_FALSE(fx.grid.editing());
    CHECK(fx.edited.size() == 1);

    // Enter 进入当前行当前列（或首个可编辑列）的编辑。
    fx.grid.setCurrentKey("r5", false);
    CHECK(fx.grid.handleKey(core::Key::Enter, core::kModifierNone));
    CHECK(fx.grid.editing());
    (void)fx.grid.commitEdit();
}

// --- 2026-09-28 第二批（设计文档 §16） ---

TEST_CASE("datagrid_pointer_clicks_carry_modifiers_and_locate_column", "[widgets][datagrid]") {
    GridFixture fx;
    fx.grid.setSelectionMode(SelectionMode::Extended);
    fx.render();

    // 单击格：替换行选择 + 定位列焦点（P0 真实指针路径）。行高走实测
    //（集合行 chrome + 内容 40），300 高视口内可点击 r0–r4。
    fx.click("grid:cell:r2:qty");
    CHECK(fx.grid.selection().currentKey() == "r2");
    CHECK(fx.grid.selection().isSelected("r2"));
    CHECK(fx.grid.currentColumn() == "qty");

    // Ctrl+单击：切换该行（保留已有选择）；列焦点随命中格。
    fx.click("grid:cell:r4:name", core::kModifierCtrl);
    CHECK(fx.grid.selection().isSelected("r4"));
    CHECK(fx.grid.selection().isSelected("r2"));
    CHECK(fx.grid.selection().selectedCount() == 2);
    CHECK(fx.grid.currentColumn() == "name");

    // Shift+单击：以锚（Ctrl 点击后的 r4）扩展连续区间，替换选择。
    fx.click("grid:cell:r1:name", core::kModifierShift);
    CHECK(fx.grid.selection().selectedCount() == 4);
    CHECK(fx.grid.selection().isSelected("r1"));
    CHECK(fx.grid.selection().isSelected("r2"));
    CHECK(fx.grid.selection().isSelected("r3"));
    CHECK(fx.grid.selection().isSelected("r4"));
}

TEST_CASE("datagrid_commit_failure_blocks_navigation_and_sort", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.grid.setCellValidator("qty", [](const std::string& text) {
        return text.find_first_not_of("0123456789") == std::string::npos
                   ? ""
                   : "digits only";
    });
    REQUIRE(fx.grid.beginEdit(0, "qty"));
    fx.shell.state().set("grid:edit", "abc");
    CHECK_FALSE(fx.grid.commitEdit());

    // 切格被拒：不覆盖草稿、保持编辑态（§13.1 失败拦截）。
    CHECK_FALSE(fx.grid.beginEdit(1, "qty"));
    CHECK(fx.grid.editing());
    CHECK(fx.shell.state().get("grid:edit") == "abc");
    CHECK(fx.grid.editError() == "digits only");

    // 行点击被拒：选择/焦点不变。
    fx.click("grid:cell:r2:name");
    CHECK(fx.grid.selection().currentKey().empty());
    CHECK(fx.grid.selection().selectedCount() == 0);
    CHECK(fx.grid.editing());

    // 排序被拒（视图变化先提交，失败中止）。
    fx.grid.requestSort("name");
    CHECK(fx.sortedBy.empty());
    CHECK(fx.grid.sortColumn().empty());

    // 修正草稿后路径恢复：提交 → 点击正常选择。
    fx.shell.state().set("grid:edit", "42");
    CHECK(fx.grid.commitEdit());
    CHECK(fx.edited.front() == "0:qty:42");
    fx.click("grid:cell:r2:name");
    CHECK(fx.grid.selection().currentKey() == "r2");

    // 同行不同列点击同样先提交（§13.1 点击其他格先提交）。
    REQUIRE(fx.grid.beginEdit(2, "qty"));
    fx.render();
    fx.click("grid:cell:r2:name");
    CHECK_FALSE(fx.grid.editing());
    REQUIRE(fx.edited.size() == 2);
    CHECK(fx.edited.back() == "2:qty:9");
    CHECK(fx.grid.currentColumn() == "name");
}

TEST_CASE("datagrid_begin_edit_focuses_editor_and_enter_commits", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    REQUIRE(fx.grid.beginEdit(0, "qty"));
    // 重建落地程序化编辑焦点（focusedBind 生效——无需点击编辑器）。
    fx.render();
    CHECK(fx.shell.controller().focusedBind() == "grid:edit");
    CHECK(fx.shell.focus().focusedKey() == "grid:editor");

    // 文本输入直接路由到编辑器（初值 "3"，光标在末尾）。
    fx.shell.textInput("4");
    CHECK(fx.shell.state().get("grid:edit") == "34");

    // 编辑态 Enter 提交；焦点回行、编辑焦点释放。
    CHECK(fx.grid.handleKey(core::Key::Enter, core::kModifierNone));
    CHECK_FALSE(fx.grid.editing());
    REQUIRE(fx.edited.size() == 1);
    CHECK(fx.edited.front() == "0:qty:34");
    CHECK(fx.shell.focus().focusedKey() == "grid:item:r0");
    CHECK(fx.shell.controller().focusedBind().empty());

    // IME composing 期间 Enter 留给输入法，不触发提交（§13.1）。
    REQUIRE(fx.grid.beginEdit(1, "qty"));
    fx.render();
    fx.shell.textEditing("9");
    CHECK_FALSE(fx.grid.handleKey(core::Key::Enter, core::kModifierNone));
    CHECK(fx.grid.editing());
    fx.shell.cancelComposition();
    CHECK(fx.grid.handleKey(core::Key::Enter, core::kModifierNone));
    CHECK_FALSE(fx.grid.editing());
    REQUIRE(fx.edited.size() == 2);
    CHECK(fx.edited.back() == "1:qty:6");
}

TEST_CASE("datagrid_checkbox_column_toggles_selection", "[widgets][datagrid]") {
    GridFixture fx;
    fx.grid.setSelectionMode(SelectionMode::Extended);
    fx.render();

    // 表头复选框：全选当前可用行。
    fx.click("grid:header-check-cell");
    CHECK(fx.grid.selection().selectedCount() == 10);
    fx.click("grid:header-check-cell");
    CHECK(fx.grid.selection().selectedCount() == 0);

    // 行复选框独立切换：不改 current（§11.3）。
    fx.grid.setCurrentKey("r0", false);
    fx.click("grid:check-cell:r3");
    CHECK(fx.grid.selection().isSelected("r3"));
    CHECK(fx.grid.selection().isSelected("r0"));
    CHECK(fx.grid.selection().currentKey() == "r0");
    fx.click("grid:check-cell:r3");
    CHECK_FALSE(fx.grid.selection().isSelected("r3"));
    CHECK(fx.grid.selection().currentKey() == "r0");
}

TEST_CASE("datagrid_column_visibility_order_and_min_width", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();

    // 显隐：构建跳过不可见列。
    CHECK(fx.grid.setColumnVisible("note", false));
    const core::Widget row0 = fx.grid.buildItem(0);
    REQUIRE(row0.children.size() == 1);
    CHECK(row0.children.front().children.size() == 3);  // 复选框 + name + qty
    // 隐藏当前列：列焦点回退首个可见列。
    CHECK(fx.grid.setColumnVisible("name", false));
    CHECK(fx.grid.currentColumn() == "qty");
    // 仅剩一个可见列：左右导航不移动也不消费。
    CHECK_FALSE(fx.grid.handleKey(core::Key::Right, core::kModifierNone));
    CHECK(fx.grid.currentColumn() == "qty");
    CHECK_FALSE(fx.grid.handleKey(core::Key::Left, core::kModifierNone));
    CHECK(fx.grid.currentColumn() == "qty");
    fx.grid.setColumnVisible("name", true);
    fx.grid.setColumnVisible("note", true);

    // 顺序：列向量顺序即表头/单元格/TSV 列序（含不可见列的既有语义）。
    CHECK(fx.grid.moveColumn("note", 0));
    CHECK(fx.grid.columns().front().key == "note");
    fx.grid.selection().setSelected({"r0"});
    CHECK(fx.grid.copySelection() == 1);
    CHECK(fx.clipboard.text() == "note 0\titem-0\t3");
    CHECK_FALSE(fx.grid.moveColumn("nope", 0));

    // minWidth：业务列下限高于全局 40；resize 钳制到 minWidth。
    auto columns = fx.grid.columns();
    columns[1].minWidth = 100.0F;  // qty
    fx.grid.setColumns(columns);
    CHECK(fx.grid.resizeColumn("qty", 50.0F));
    const auto widths = fx.grid.columnWidths();
    REQUIRE(widths.size() == 3);
    CHECK(widths[1].second == 100.0F);
}

TEST_CASE("datagrid_double_click_cell_starts_edit_and_enter_activates", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();

    // 双击可编辑格：原地进入编辑（400ms 窗口内同一格两次点击）。
    fx.shell.tick(100);
    fx.click("grid:cell:r1:qty");
    fx.shell.tick(220);
    fx.click("grid:cell:r1:qty");
    CHECK(fx.grid.editing());
    CHECK(fx.shell.state().get("grid:edit") == "6");
    fx.grid.cancelEdit();

    // Enter：当前列不可编辑时回退行激活（§6）。
    fx.grid.setCurrentKey("r2", false);
    fx.grid.setCurrentColumn("name");  // 只读列
    CHECK(fx.grid.handleKey(core::Key::Enter, core::kModifierNone));
    CHECK_FALSE(fx.grid.editing());
    REQUIRE(fx.activated.size() == 1);
    CHECK(fx.activated.front() == "r2");

    // Enter：当前列可编辑时进入编辑。
    fx.grid.setCurrentColumn("qty");
    CHECK(fx.grid.handleKey(core::Key::Enter, core::kModifierNone));
    CHECK(fx.grid.editing());
}

TEST_CASE("datagrid_numeric_column_aligns_end_and_custom_empty_state", "[widgets][datagrid]") {
    GridFixture fx;
    auto columns = fx.grid.columns();
    columns[1].align = widgets::DataColumnAlign::End;  // qty 数值列右对齐
    fx.grid.setColumns(columns);
    const core::Widget row0 = fx.grid.buildItem(0);
    const core::Widget& content = row0.children.front();
    REQUIRE(content.children.size() == 4);
    // §18 统一格式盒：全部列为 Container（key 后缀 :box）；End 列盒内
    // 是主轴 End 的 Row（内嵌省略文本），Start 列盒内是裸文本。
    CHECK(content.children[1].type == core::WidgetType::Container);
    CHECK(content.children[2].type == core::WidgetType::Container);
    const core::Widget& qtyAlign = content.children[2].children.front();
    CHECK(qtyAlign.type == core::WidgetType::Row);
    CHECK(qtyAlign.mainAxis == core::MainAxisAlignment::End);
    CHECK(qtyAlign.children.front().textStyle.overflow ==
          core::TextOverflow::Ellipsis);

    // 自定义空态（数据状态壳由应用组合，§14）。
    fx.grid.setEmptyBuilder([] {
        auto text = core::makeText("正在加载…");
        return core::makeColumn({std::move(text)});
    });
    fx.grid.setRowCount(0);
    fx.render();
    const auto* empty = core::findNodeByKey(fx.shell.root(), "grid:empty");
    REQUIRE(empty != nullptr);
    REQUIRE(!empty->children.empty());
    CHECK(empty->children.front().text == "正在加载…");
}

// --- 回归验证（review 发现：先证实再修复） ---

TEST_CASE("REGRESSION_editor_click_must_not_commit_draft", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    REQUIRE(fx.grid.beginEdit(0, "qty"));
    fx.render();
    fx.shell.state().set("grid:edit", "42");
    // 在编辑器内单击（重新定位光标）：不得提交草稿。
    fx.click("grid:editor");
    CHECK(fx.grid.editing());
    CHECK(fx.edited.empty());
    CHECK(fx.shell.controller().focusedBind() == "grid:edit");
    // 在编辑器内双击（激活路径冒泡到行）：同样不打断草稿。
    fx.shell.tick(500);
    fx.click("grid:editor");
    fx.shell.tick(100);
    fx.click("grid:editor");
    CHECK(fx.grid.editing());
    CHECK(fx.edited.empty());
    CHECK(fx.shell.state().get("grid:edit") == "42");
}

TEST_CASE("REGRESSION_tab_commits_and_moves_editor_cell", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    // 可编辑列序 = [qty, note]；从 (r0, qty) Tab → (r0, note)。
    REQUIRE(fx.grid.beginEdit(0, "qty"));
    fx.render();
    fx.shell.state().set("grid:edit", "42");
    CHECK(fx.grid.handleKey(core::Key::Tab, core::kModifierNone));
    CHECK(fx.grid.editing());
    REQUIRE(fx.edited.size() == 1);
    CHECK(fx.edited.front() == "0:qty:42");
    fx.render();
    CHECK(fx.shell.state().get("grid:edit") == "note 0");
    // 跨行：(r0, note) Tab → (r1, qty)。
    CHECK(fx.grid.handleKey(core::Key::Tab, core::kModifierNone));
    fx.render();
    CHECK(fx.shell.state().get("grid:edit") == "6");
    // Shift+Tab 反向：回到 (r0, note)。
    CHECK(fx.grid.handleKey(core::Key::Tab, core::kModifierShift));
    fx.render();
    CHECK(fx.shell.state().get("grid:edit") == "note 0");
    // 提交失败中止移动：qty 校验失败后 Tab 留在编辑器。
    fx.grid.setCellValidator("qty", [](const std::string& text) {
        return text.find_first_not_of("0123456789") == std::string::npos
                   ? ""
                   : "digits only";
    });
    fx.grid.cancelEdit();
    REQUIRE(fx.grid.beginEdit(0, "qty"));
    fx.render();
    fx.shell.state().set("grid:edit", "abc");
    CHECK(fx.grid.handleKey(core::Key::Tab, core::kModifierNone));
    CHECK(fx.grid.editing());
    CHECK(fx.shell.state().get("grid:edit") == "abc");
    // 修正后 Tab：先到同行 (r0, note)；再 Tab 跨行跳过禁用行 r1
    // → (r2, qty)。
    fx.grid.setRowEnabledOf([](std::size_t i) { return i != 1; });
    fx.shell.state().set("grid:edit", "7");
    CHECK(fx.grid.handleKey(core::Key::Tab, core::kModifierNone));
    fx.render();
    CHECK(fx.shell.state().get("grid:edit") == "note 0");
    CHECK(fx.grid.handleKey(core::Key::Tab, core::kModifierNone));
    fx.render();
    CHECK(fx.shell.state().get("grid:edit") == "9");
    // 边界：(r9, note) Tab → 提交并停在当前格（退出编辑）。
    fx.grid.cancelEdit();
    REQUIRE(fx.grid.beginEdit(9, "note"));
    fx.render();
    CHECK(fx.grid.handleKey(core::Key::Tab, core::kModifierNone));
    CHECK_FALSE(fx.grid.editing());
    CHECK(fx.shell.focus().focusedKey() == "grid:item:r9");
}

TEST_CASE("REGRESSION_column_ops_commit_edit_and_abort_on_failure", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.grid.setCellValidator("qty", [](const std::string& text) {
        return text.find_first_not_of("0123456789") == std::string::npos
                   ? ""
                   : "digits only";
    });
    // 隐藏列先提交：校验失败中止显隐变更。
    REQUIRE(fx.grid.beginEdit(0, "qty"));
    fx.render();
    fx.shell.state().set("grid:edit", "abc");
    CHECK_FALSE(fx.grid.setColumnVisible("qty", false));
    CHECK(fx.grid.editing());
    CHECK(fx.grid.columns()[1].visible);
    // 移动列同样先提交、失败中止。
    CHECK_FALSE(fx.grid.moveColumn("qty", 0));
    CHECK(fx.grid.editing());
    // 修正草稿后放行：提交 → 变更生效、编辑退出。
    fx.shell.state().set("grid:edit", "42");
    CHECK(fx.grid.setColumnVisible("qty", false));
    CHECK_FALSE(fx.grid.editing());
    REQUIRE(fx.edited.size() == 1);
    CHECK(fx.edited.front() == "0:qty:42");
    CHECK_FALSE(fx.grid.columns()[1].visible);
    // 移动列（无编辑态时直接生效）。
    CHECK(fx.grid.moveColumn("note", 0));
    CHECK(fx.grid.columns().front().key == "note");
}

// --- 2026-09-28 第三批（设计文档 §17：双轴几何 + 列宽手柄） ---

namespace {
// 手柄中心（树随列宽变化重排，每次交互前重取）。
core::Offset handleCenter(const app::AppShell& shell, const char* key) {
    const core::RenderNode* node = core::findNodeByKey(shell.root(), key);
    REQUIRE(node != nullptr);
    return core::absoluteOffset(shell.root(), key) +
           core::Offset{node->size.width * 0.5F,
                        node->size.height * 0.5F};
}
}  // namespace

TEST_CASE("datagrid_horizontal_viewport_scrolls_header_and_rows_together",
          "[widgets][datagrid]") {
    GridFixture fx;
    // 加宽到超视口：44 + 300 + 80 + 140 = 564 > 400。
    CHECK(fx.grid.resizeColumn("name", 300.0F));
    fx.render();
    fx.render();  // 视口宽跟踪收敛
    const auto* view = core::findNodeByKey(fx.shell.root(), "grid");
    REQUIRE(view != nullptr);
    CHECK(view->scrollAxis == core::ScrollAxis::Horizontal);
    CHECK(view->scrollExtent == Catch::Approx(164.0F));

    // 横向滚轮（数据区，纯 x 分量）：框架经源接缝直驱 hScroll_，表头与
    // 数据行同源平移（共享横向 offset）。
    const core::Offset headerBefore =
        core::absoluteOffset(fx.shell.root(), "grid:header");
    const core::Offset cellBefore =
        core::absoluteOffset(fx.shell.root(), "grid:cell:r0:name");
    CHECK(fx.shell.wheel(core::Offset{200.0F, 150.0F},
                         core::Offset{60.0F, 0.0F}));
    fx.render();
    CHECK(fx.grid.hScroll().offset() == Catch::Approx(60.0F));
    const core::Offset headerAfter =
        core::absoluteOffset(fx.shell.root(), "grid:header");
    const core::Offset cellAfter =
        core::absoluteOffset(fx.shell.root(), "grid:cell:r0:name");
    CHECK(headerAfter.x == Catch::Approx(headerBefore.x - 60.0F));
    CHECK(cellAfter.x == Catch::Approx(cellBefore.x - 60.0F));

    // 纵向滚轮仍归数据列表（双轴互不抢占）；Shift+纵轮在表头区（命中
    // 链上无纵向视口）投影到横向视口（lumen-scroll-design §4）。
    const float vBefore = fx.grid.scroll().offset();
    CHECK(fx.shell.wheel(core::Offset{200.0F, 150.0F},
                         core::Offset{0.0F, 60.0F}));
    fx.render();
    CHECK(fx.grid.scroll().offset() > vBefore);
    CHECK(fx.grid.hScroll().offset() == Catch::Approx(60.0F));
    CHECK(fx.shell.wheel(core::Offset{200.0F, 18.0F},
                         core::Offset{0.0F, 40.0F}, core::kModifierShift));
    fx.render();
    CHECK(fx.grid.hScroll().offset() == Catch::Approx(100.0F));
}

TEST_CASE("datagrid_rows_fill_viewport_when_content_narrower",
          "[widgets][datagrid]") {
    GridFixture fx;  // 内容 44+100+80+140 = 364 < 视口 400
    fx.render();
    fx.render();     // 视口宽跟踪收敛（铺满不出现尾部空隙）
    const auto* row = core::findNodeByKey(fx.shell.root(), "grid:item:r0");
    REQUIRE(row != nullptr);
    // 行宽 = List 宽 − 容器水平内边距 2（集合行既有口径）。
    CHECK(row->size.width == Catch::Approx(398.0F));
    CHECK(core::findNodeByKey(fx.shell.root(), "grid")->scrollExtent ==
          Catch::Approx(0.0F));

    // 加宽超视口后：行宽 = 内容宽（564 − 内边距 2），出现横向滚动范围。
    CHECK(fx.grid.resizeColumn("name", 300.0F));
    fx.render();
    const auto* wide = core::findNodeByKey(fx.shell.root(), "grid:item:r0");
    REQUIRE(wide != nullptr);
    CHECK(wide->size.width == Catch::Approx(562.0F));
    CHECK(core::findNodeByKey(fx.shell.root(), "grid")->scrollExtent ==
          Catch::Approx(164.0F));

    // 密度切换：Theme.dataGrid 驱动表头高（Compact 32，视口宽跟踪重算）。
    fx.shell.setTheme(style::Theme::dark(style::ControlDensity::Compact));
    fx.render();
    const auto* header = core::findNodeByKey(fx.shell.root(), "grid:header");
    REQUIRE(header != nullptr);
    CHECK(header->size.height == Catch::Approx(32.0F));
}

TEST_CASE("datagrid_column_resize_handle_drag_clamps_and_resets",
          "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.render();
    const auto* handle = core::findNodeByKey(fx.shell.root(), "grid:hnd:name");
    REQUIRE(handle != nullptr);
    REQUIRE(handle->splitterSource != nullptr);  // splitter 通道接线
    // 手柄带宽 = resizeHitWidth（Comfortable 10），列宽预算内。
    CHECK(handle->size.width == Catch::Approx(10.0F));

    // 拖动 +80：name 100 → 180（绝对边界语义，跟手）。
    core::Offset at = handleCenter(fx.shell, "grid:hnd:name");
    fx.shell.pointerDown(at);
    fx.shell.pointerMove(core::Offset{at.x + 80.0F, at.y});
    CHECK(fx.grid.columns()[0].width == Catch::Approx(180.0F));
    fx.shell.pointerUp(core::Offset{at.x + 80.0F, at.y});
    fx.render();

    // 拖过头：顶住 minWidth（默认 40）。
    fx.shell.tick(1000);  // 避开双击窗口
    at = handleCenter(fx.shell, "grid:hnd:name");
    fx.shell.pointerDown(at);
    fx.shell.pointerMove(core::Offset{at.x - 1000.0F, at.y});
    CHECK(fx.grid.columns()[0].width == Catch::Approx(40.0F));
    fx.shell.pointerUp(core::Offset{at.x - 1000.0F, at.y});
    fx.render();

    // 双击手柄复位：回 setColumns 初始宽 100（splitter reset 通道）。
    // tick 为绝对时间戳：第二击须在首击之后且间隔 ≤ 双击窗口 400ms。
    fx.shell.tick(1000);
    fx.click("grid:hnd:name");
    fx.shell.tick(1300);
    fx.click("grid:hnd:name");
    CHECK(fx.grid.columns()[0].width == Catch::Approx(100.0F));
}

TEST_CASE("datagrid_resize_handle_keyboard_steps_and_tab_stop",
          "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.render();
    // Tab 序（键盘可达性）：横向视口根 → 表头排序钮 → 首个手柄。
    fx.shell.keyDown(core::Key::Tab);
    fx.shell.keyDown(core::Key::Tab);
    fx.shell.keyDown(core::Key::Tab);
    CHECK(fx.shell.focus().focusedKey() == "grid:hnd:name");
    // 方向键步进 = 手柄带宽（Comfortable 10px）。
    fx.shell.keyDown(core::Key::Right);
    CHECK(fx.grid.columns()[0].width == Catch::Approx(110.0F));
    fx.shell.keyDown(core::Key::Left);
    CHECK(fx.grid.columns()[0].width == Catch::Approx(100.0F));
    // Home 收缩到 minWidth；End 无上界语义（列宽无 max），不动作。
    fx.shell.keyDown(core::Key::Home);
    CHECK(fx.grid.columns()[0].width == Catch::Approx(40.0F));
    fx.shell.keyDown(core::Key::End);
    CHECK(fx.grid.columns()[0].width == Catch::Approx(40.0F));
}

TEST_CASE("datagrid_resize_commits_edit_and_blocks_on_failure",
          "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.grid.setCellValidator("qty", [](const std::string& text) {
        return text.find_first_not_of("0123456789") == std::string::npos
                   ? ""
                   : "digits only";
    });
    REQUIRE(fx.grid.beginEdit(0, "qty"));
    fx.render();
    fx.shell.state().set("grid:edit", "abc");

    // 手柄拖动先提交（§13.1 视图变化先提交）：失败中止——列宽不动、
    // 编辑保留、草稿不丢。
    const core::Offset at = handleCenter(fx.shell, "grid:hnd:qty");
    fx.shell.pointerDown(at);
    fx.shell.pointerMove(core::Offset{at.x - 50.0F, at.y});
    CHECK(fx.grid.columns()[1].width == Catch::Approx(80.0F));
    CHECK(fx.grid.editing());
    CHECK(fx.shell.state().get("grid:edit") == "abc");
    fx.shell.pointerUp(core::Offset{at.x - 50.0F, at.y});

    // 修正草稿后再拖：提交生效 + 列宽变化。
    fx.shell.tick(1000);
    fx.shell.state().set("grid:edit", "42");
    const core::Offset again = handleCenter(fx.shell, "grid:hnd:qty");
    fx.shell.pointerDown(again);
    fx.shell.pointerMove(core::Offset{again.x + 50.0F, again.y});
    CHECK(fx.grid.columns()[1].width == Catch::Approx(130.0F));
    CHECK_FALSE(fx.grid.editing());
    fx.shell.pointerUp(core::Offset{again.x + 50.0F, again.y});
    REQUIRE(fx.edited.size() == 1);
    CHECK(fx.edited.front() == "0:qty:42");
}

// --- 2026-09-28 第四批（设计文档 §18：多列排序 + 当前格环 + 表头三态） ---

TEST_CASE("datagrid_multi_sort_shift_appends_and_cycles", "[widgets][datagrid]") {
    GridFixture fx;
    auto columns = fx.grid.columns();
    columns[1].sortable = true;  // qty 也参与排序
    fx.grid.setColumns(columns);
    std::vector<std::string> multi{};
    fx.grid.onSortRequestMulti =
        [&](const std::vector<widgets::SortKey>& keys) {
            multi.clear();
            for (const auto& key : keys) {
                multi.push_back(key.columnKey +
                                (key.ascending ? "+" : "-"));
            }
        };

    // 单列起步（既有契约）；Shift 追加 qty 为最低优先级。
    fx.grid.requestSort("name");
    fx.grid.requestSort("qty", /*extend=*/true);
    REQUIRE(fx.grid.sortKeys().size() == 2);
    CHECK(fx.grid.sortKeys()[0].columnKey == "name");
    CHECK(fx.grid.sortKeys()[1].columnKey == "qty");
    CHECK(fx.grid.sortKeys()[1].ascending);
    REQUIRE(multi.size() == 2);
    CHECK(multi[0] == "name+");
    CHECK(multi[1] == "qty+");
    // 兼容回调携带主排序键。
    CHECK(fx.grid.sortColumn() == "name");
    CHECK(fx.grid.sortAscending());
    REQUIRE(fx.sortedBy.back() == "name+");

    // Shift 循环：qty 升 → 降 → 移除（序号连续）。
    fx.grid.requestSort("qty", true);
    CHECK_FALSE(fx.grid.sortKeys()[1].ascending);
    CHECK(multi.back() == "qty-");
    fx.grid.requestSort("qty", true);
    REQUIRE(fx.grid.sortKeys().size() == 1);
    CHECK(fx.grid.sortKeys().front().columnKey == "name");

    // 多列在位时普通点击收敛为单列升序（§11.2 替换排序列表）。
    fx.grid.requestSort("qty", true);
    REQUIRE(fx.grid.sortKeys().size() == 2);
    fx.grid.requestSort("name");
    REQUIRE(fx.grid.sortKeys().size() == 1);
    CHECK(fx.grid.sortKeys().front().columnKey == "name");
    CHECK(fx.grid.sortKeys().front().ascending);

    // 指针路径：Shift+点击表头追加（修饰键透传到 extend 语义）。
    fx.render();
    fx.click("grid:head:qty", core::kModifierShift);
    REQUIRE(fx.grid.sortKeys().size() == 2);
    CHECK(fx.grid.sortKeys()[1].columnKey == "qty");

    // 列集移除排序列：键失效清除、序号连续。
    auto shrink = fx.grid.columns();
    shrink[1].sortable = false;
    shrink.erase(shrink.begin() + 1);
    fx.grid.setColumns(shrink);
    REQUIRE(fx.grid.sortKeys().size() == 1);
    CHECK(fx.grid.sortKeys().front().columnKey == "name");
}

TEST_CASE("datagrid_current_cell_ring_follows_current_cell",
          "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    const core::Color ring = fx.shell.theme().colors.focusRing;
    const float ringWidth = fx.shell.theme().metrics.focusRingWidth;
    fx.grid.setCurrentKey("r2", false);
    fx.grid.setCurrentColumn("qty");
    fx.render();

    // (r2, qty) = 当前格：格式盒边框承载 focusRing token（§12 内嵌环）。
    const auto* ringed =
        core::findNodeByKey(fx.shell.root(), "grid:cell:r2:qty:box");
    REQUIRE(ringed != nullptr);
    CHECK(ringed->commonStyle().border == ring);
    CHECK(ringed->commonStyle().borderWidth == ringWidth);
    // 同行其他格不带环。
    const auto* plain =
        core::findNodeByKey(fx.shell.root(), "grid:cell:r2:name:box");
    REQUIRE(plain != nullptr);
    CHECK(plain->commonStyle().borderWidth == 0.0F);
    CHECK(plain->commonStyle().border == core::Color::transparent());

    // current 移动：环跟随到新格、旧格恢复无环。
    fx.grid.setCurrentKey("r3", false);
    fx.render();
    const auto* next =
        core::findNodeByKey(fx.shell.root(), "grid:cell:r3:qty:box");
    REQUIRE(next != nullptr);
    CHECK(next->commonStyle().border == ring);
    const auto* previous =
        core::findNodeByKey(fx.shell.root(), "grid:cell:r2:qty:box");
    REQUIRE(previous != nullptr);
    CHECK(previous->commonStyle().borderWidth == 0.0F);

    // 格式盒无条件存在（identity 稳定）：双击当前格仍进入编辑（回归）。
    fx.shell.tick(1000);
    fx.click("grid:cell:r1:qty");
    fx.shell.tick(1200);
    fx.click("grid:cell:r1:qty");
    CHECK(fx.grid.editing());
    CHECK(fx.shell.state().get("grid:edit") == "6");
}

TEST_CASE("datagrid_header_check_three_state_semantics", "[widgets][datagrid]") {
    GridFixture fx;
    fx.grid.setSelectionMode(SelectionMode::Extended);
    fx.render();

    // 部分选中：indeterminate（accent 填充 + 横线；语义 value="mixed"）。
    fx.grid.selection().setSelected({"r0", "r3"});
    fx.render();
    const auto* check =
        core::findNodeByKey(fx.shell.root(), "grid:header-check");
    REQUIRE(check != nullptr);
    CHECK_FALSE(check->checked);
    CHECK(check->indeterminate);
    const auto* resolved = std::get_if<core::CheckboxResolvedStyle>(
        &check->style.component);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->indeterminate);

    accessibility::SemanticsBuildOptions options;
    accessibility::SemanticsTree tree =
        accessibility::buildSemanticsTree(fx.shell.root(), options);
    const auto* node = tree.find(check->identity);
    REQUIRE(node != nullptr);
    CHECK(node->value == "mixed");
    CHECK((node->flags & accessibility::kSemanticsChecked) == 0);

    // 全选：checked（value="true"）；无选中：未勾选（value="false"）。
    fx.click("grid:header-check-cell");
    fx.render();
    const auto* all =
        core::findNodeByKey(fx.shell.root(), "grid:header-check");
    REQUIRE(all != nullptr);
    CHECK(all->checked);
    CHECK_FALSE(all->indeterminate);
    tree = accessibility::buildSemanticsTree(fx.shell.root(), options);
    const auto* allNode = tree.find(all->identity);
    REQUIRE(allNode != nullptr);
    CHECK(allNode->value == "true");
    CHECK((allNode->flags & accessibility::kSemanticsChecked) != 0);
}
