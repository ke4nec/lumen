// M14-D：DataGrid 契约测试（docs/lumen-datagrid-design.md §8/§16）。
//
// 覆盖首版契约切片：列模型与列宽调整、行虚拟化物化、选择（共享
// SelectionModel）、键盘导航与列焦点、排序回调与状态、TSV 复制粘贴、
// 单元格编辑与校验。2026-09-28 第二批：真实指针 Ctrl/Shift 修饰键路径、
// 单元格点击定位列、提交失败拦截（切格/切行/排序）、编辑器程序化焦点
// 与编辑态 Enter 提交（IME composing 除外）、排序升→降→清除循环、列
// 显隐/顺序/minWidth、复选框选择列、双击进入编辑。
// 水平虚拟化/RTL/拖放为后续增量（不在本文件断言）。

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
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
    // 单元格契约（§12/§16）：固定列宽 + 水平内边距 12 + 单行省略。
    const core::Widget row0 = fx.grid.buildItem(0);
    REQUIRE(row0.children.size() == 1);
    const core::Widget& content = row0.children.front();
    // [复选框格, name, qty, note]。
    REQUIRE(content.children.size() == 4);
    CHECK(content.children.front().width.value_or(0.0F) == 44.0F);
    const core::Widget& nameCell = content.children[1];
    CHECK(nameCell.width.value_or(0.0F) == 100.0F);
    CHECK(nameCell.padding.left == 12.0F);
    CHECK(nameCell.textStyle.overflow == core::TextOverflow::Ellipsis);
    CHECK(nameCell.textStyle.maxLines == 1);
    CHECK(nameCell.onClick == "grid:grid:cell:r0:name");
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
    // End 列 = Row 盒（主轴 End）内嵌省略文本；Start 列为裸文本。
    CHECK(content.children[1].type == core::WidgetType::Text);
    CHECK(content.children[2].type == core::WidgetType::Row);
    CHECK(content.children[2].mainAxis == core::MainAxisAlignment::End);
    CHECK(content.children[2].children.front().textStyle.overflow ==
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
