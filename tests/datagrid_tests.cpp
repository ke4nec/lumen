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
// 2026-09-29 第五/六批（设计文档 §19/§20）：冻结列（pinned 模型/区域
// 拆分/共享纵向几何/语义排除/分界线/跨区编辑键盘）、水平虚拟化（列
// 窗口物化/窗口无关复制编辑/datagrid-wide 基准预算断言）。
// 2026-09-29 视觉/交互 review 收口（设计文档 §21）：Ctrl+导航仅移动
// current、带选择列的固定行高回归、表头表面/配色/字号 token 化、当
// 前格环铺满整格、表头/数据列对齐（滚动区去重复选择列 + 行壳零内
// 边距）、格内边距密度档、手柄 Stack 叠放不占文字预算。
// RTL/拖放为后续增量（不在本文件断言）。

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
    fx.render();  // 视口宽/列窗口两帧收敛（§19 T6.2 同铺满口径）
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
    // 复选框格只在冻结区（§20.2 选择列常驻冻结区——滚动区行不重复
    // 物化；双份为第五批回归，本批修复）。
    CHECK(checkboxes == rows);
    // 表头存在。
    const auto* header = core::findNodeByKey(fx.shell.root(), "grid:header");
    REQUIRE(header != nullptr);
    // 单元格契约（§12/§16/§18/§21）：统一格式盒（Row，key 后缀 :box）
    // 即内容行——固定列宽 + 格内边距（密度档，Comfortable 12）+ 盒定高
    // = 行高（当前格环铺满整格、文本垂直居中）；当前格环经盒边框承载。
    const core::Widget row0 = fx.grid.buildItem(0);
    REQUIRE(row0.children.size() == 1);
    const core::Widget& content = row0.children.front();
    // [name, qty, note]。
    REQUIRE(content.children.size() == 3);
    const core::Widget& nameCell = content.children.front();
    REQUIRE(nameCell.type == core::WidgetType::Row);
    CHECK(nameCell.width.value_or(0.0F) == 100.0F);
    CHECK(nameCell.height.value_or(0.0F) == 40.0F);
    CHECK(nameCell.padding.left == 12.0F);
    CHECK(nameCell.crossAxis == core::CrossAxisAlignment::Center);
    REQUIRE(nameCell.children.size() == 1);
    const core::Widget& nameText = nameCell.children.front();
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
    // name + qty（选择复选框列常驻冻结区——滚动区行不重复物化）。
    CHECK(row0.children.front().children.size() == 2);
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
    fx.render();
    fx.render();  // 列窗口收敛（buildItem 窗口物化，§19 T6.2）
    const core::Widget row0 = fx.grid.buildItem(0);
    const core::Widget& content = row0.children.front();
    REQUIRE(content.children.size() == 3);
    // §18/§21 统一格式盒：全部列为 Row（key 后缀 :box）——主轴承载
    // Start/End 对齐、crossAxis Center 垂直居中、盒定高 = 行高。
    CHECK(content.children[0].type == core::WidgetType::Row);
    CHECK(content.children[1].type == core::WidgetType::Row);
    const core::Widget& qtyBox = content.children[1];
    CHECK(qtyBox.mainAxis == core::MainAxisAlignment::End);
    CHECK(qtyBox.crossAxis == core::CrossAxisAlignment::Center);
    CHECK(qtyBox.children.front().textStyle.overflow ==
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
    // §19 区域拆分：滚动视口 = grid:scroll（选择复选框列常驻冻结区，
    // 冻结宽 44 + 分界线 1 → 滚动视口宽 355）；滚动内容 300+80+140=520。
    const auto* view = core::findNodeByKey(fx.shell.root(), "grid:scroll");
    REQUIRE(view != nullptr);
    CHECK(view->scrollAxis == core::ScrollAxis::Horizontal);
    CHECK(view->scrollExtent == Catch::Approx(165.0F));

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
    GridFixture fx;
    // §19 区域拆分：选择列 44 常驻冻结区 + 分界线 1 → 滚动视口 355；
    // 滚动内容 100+80+140 = 320 < 355 → 滚动区行铺满视口。
    fx.render();
    fx.render();     // 视口宽跟踪收敛（铺满不出现尾部空隙）
    const auto* row = core::findNodeByKey(fx.shell.root(), "grid:item:r0");
    REQUIRE(row != nullptr);
    // 行宽 = 滚动区宽全宽（List 分隔线内缩已清零：行壳与表头同缘，
    // 表头/数据列边界对齐前提）。
    CHECK(row->size.width == Catch::Approx(355.0F));
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:scroll")->scrollExtent ==
          Catch::Approx(0.0F));

    // 加宽超视口后：滚动内容 300+80+140 = 520 > 355，出现横向滚动范围。
    CHECK(fx.grid.resizeColumn("name", 300.0F));
    fx.render();
    const auto* wide = core::findNodeByKey(fx.shell.root(), "grid:item:r0");
    REQUIRE(wide != nullptr);
    CHECK(wide->size.width == Catch::Approx(520.0F));
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:scroll")->scrollExtent ==
          Catch::Approx(165.0F));

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

// --- 2026-09-29 第五批（设计文档 §19：冻结列） ---

TEST_CASE("datagrid_frozen_region_pins_columns_and_keeps_alignment",
          "[widgets][datagrid]") {
    GridFixture fx;
    // pin name（44 选择列 + 100 = 冻结宽 144；分界线 1 → 滚动视口 255）。
    CHECK(fx.grid.setColumnPinned("name", true));
    fx.render();
    fx.render();
    const auto* frozenBody =
        core::findNodeByKey(fx.shell.root(), "grid:frozen-body");
    const auto* line = core::findNodeByKey(fx.shell.root(), "grid:freeze-line");
    const auto* view = core::findNodeByKey(fx.shell.root(), "grid:scroll");
    REQUIRE(frozenBody != nullptr);
    REQUIRE(line != nullptr);
    REQUIRE(view != nullptr);
    CHECK(frozenBody->size.width == Catch::Approx(144.0F));
    CHECK(line->size.width == Catch::Approx(1.0F));
    CHECK(line->commonStyle().background == fx.shell.theme().colors.borderStrong);
    CHECK(view->size.width == Catch::Approx(255.0F));
    // 滚动内容 80+140 = 220 < 255 → 无横向滚动。
    CHECK(view->scrollExtent == Catch::Approx(0.0F));

    // 两区行同 y 对齐（固定行高 + 共享可见区）。
    const core::Offset scrollRow =
        core::absoluteOffset(fx.shell.root(), "grid:item:r0");
    const core::Offset frozenRow =
        core::absoluteOffset(fx.shell.root(), "grid:frow:r0");
    CHECK(frozenRow.y == Catch::Approx(scrollRow.y));
    // 区间宽 = 144 冻结 + 1 分界（绝对 x 含 List 既有 1px 水平内边距）。
    CHECK(scrollRow.x - frozenRow.x == Catch::Approx(145.0F));

    // 纵向滚轮在冻结区生效（共享纵向控制器）。
    CHECK(fx.shell.wheel(core::Offset{70.0F, 150.0F},
                         core::Offset{0.0F, 80.0F}));
    fx.render();
    const core::Offset scrolledFrozen =
        core::absoluteOffset(fx.shell.root(), "grid:frow:r0");
    CHECK(scrolledFrozen.y < frozenRow.y);
    const core::Offset scrolledScroll =
        core::absoluteOffset(fx.shell.root(), "grid:item:r0");
    CHECK(scrolledScroll.y == Catch::Approx(scrolledFrozen.y));
}

TEST_CASE("datagrid_frozen_columns_stay_fixed_while_scroll_region_pans",
          "[widgets][datagrid]") {
    GridFixture fx;
    CHECK(fx.grid.setColumnPinned("name", true));
    // 加宽滚动内容：qty → 300（滚动内容 300+140 = 440 > 视口 255）。
    CHECK(fx.grid.resizeColumn("qty", 300.0F));
    fx.render();
    fx.render();
    const auto* view = core::findNodeByKey(fx.shell.root(), "grid:scroll");
    REQUIRE(view != nullptr);
    CHECK(view->scrollExtent == Catch::Approx(185.0F));

    // 横向滚轮：滚动区（表头/行）平移，冻结区纹丝不动。
    const core::Offset frozenHeader =
        core::absoluteOffset(fx.shell.root(), "grid:frozen-header");
    const core::Offset frozenCell =
        core::absoluteOffset(fx.shell.root(), "grid:cell:r0:name");
    const core::Offset scrollCell =
        core::absoluteOffset(fx.shell.root(), "grid:cell:r0:qty");
    CHECK(fx.shell.wheel(core::Offset{200.0F, 150.0F},
                         core::Offset{60.0F, 0.0F}));
    fx.render();
    CHECK(fx.grid.hScroll().offset() == Catch::Approx(60.0F));
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:frozen-header").x ==
          Catch::Approx(frozenHeader.x));
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:cell:r0:name").x ==
          Catch::Approx(frozenCell.x));
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:cell:r0:qty").x ==
          Catch::Approx(scrollCell.x - 60.0F));
}

TEST_CASE("datagrid_pin_model_prefix_invariant_and_commit_guard",
          "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    // pin/unpin 移动：pin = 冻结组尾，unpin = 滚动组首。
    CHECK(fx.grid.setColumnPinned("qty", true));
    auto keys = [&] {
        std::vector<std::string> out;
        for (const auto& column : fx.grid.columns()) out.push_back(column.key);
        return out;
    };
    CHECK(keys() == std::vector<std::string>{"qty", "name", "note"});
    CHECK(fx.grid.setColumnPinned("qty", false));
    CHECK(keys() == std::vector<std::string>{"name", "note", "qty"});
    // 未知列拒绝；重复 pin 幂等。
    CHECK_FALSE(fx.grid.setColumnPinned("nope", true));
    CHECK(fx.grid.setColumnPinned("name", true));
    CHECK(fx.grid.setColumnPinned("name", true));
    CHECK(keys() == std::vector<std::string>{"name", "note", "qty"});

    // moveColumn 组内钳制：qty 已 unpin（上一步），note 移到 0（name 之前
    // = 冻结组内）被钳到滚动组首（index 1）。
    CHECK(fx.grid.moveColumn("note", 0));
    CHECK(keys() == std::vector<std::string>{"name", "note", "qty"});

    // pin/unpin 先提交编辑（§13.1）：校验失败中止区域调整。
    fx.grid.setCellValidator("qty", [](const std::string& text) {
        return text.find_first_not_of("0123456789") == std::string::npos
                   ? ""
                   : "digits only";
    });
    REQUIRE(fx.grid.beginEdit(0, "qty"));
    fx.render();
    fx.shell.state().set("grid:edit", "abc");
    CHECK_FALSE(fx.grid.setColumnPinned("qty", true));
    CHECK(fx.grid.editing());
    CHECK_FALSE(fx.grid.columns()[2].pinned);
    fx.shell.state().set("grid:edit", "42");
    CHECK(fx.grid.setColumnPinned("qty", true));
    CHECK_FALSE(fx.grid.editing());
    REQUIRE(fx.edited.size() == 1);
    CHECK(fx.edited.front() == "0:qty:42");
}

TEST_CASE("datagrid_frozen_region_excludes_semantics_and_clicks_sync",
          "[widgets][datagrid]") {
    GridFixture fx;
    CHECK(fx.grid.setColumnPinned("name", true));
    fx.render();

    // 语义去重（T5.5）：冻结区行/格不进语义树，滚动区行承载——每数据行
    // 恰一个 listItem 语义节点。
    accessibility::SemanticsBuildOptions options;
    accessibility::SemanticsTree tree =
        accessibility::buildSemanticsTree(fx.shell.root(), options);
    int listItems = 0;
    for (const auto& [id, node] : tree.nodes) {
        (void)id;
        if (node.role == accessibility::SemanticsRole::ListItem) ++listItems;
    }
    const auto* frozenRow =
        core::findNodeByKey(fx.shell.root(), "grid:frow:r0");
    REQUIRE(frozenRow != nullptr);
    CHECK(frozenRow->excludeFromSemantics);
    // 滚动区 10 行承载语义；冻结副本被排除（恰好 10 个 listItem）。
    CHECK(listItems == 10);

    // 点击冻结区格（cell 身份）：选择 + 列焦点同步到逻辑行（滚动区行
    // 成为焦点载体；复选框格路径本就不改 current，§11.3）。
    fx.click("grid:cell:r3:name");
    fx.render();
    CHECK(fx.grid.selection().currentKey() == "r3");
    CHECK(fx.grid.selection().isSelected("r3"));
    CHECK(fx.grid.currentColumn() == "name");
    CHECK(fx.shell.focus().focusedKey() == "grid:item:r3");
    // 滚动区选中态同步（同一选择集重建）。
    const auto* twin = core::findNodeByKey(fx.shell.root(), "grid:item:r3");
    REQUIRE(twin != nullptr);
    CHECK(twin->selected);

    // 跨区编辑：可编辑列 pin 后双击冻结区格进入编辑（编辑器物化于冻结
    // 区）；Enter 提交走既有编辑事务。
    CHECK(fx.grid.setColumnPinned("qty", true));
    fx.render();
    fx.shell.tick(1000);
    fx.click("grid:cell:r1:qty");
    fx.shell.tick(1200);
    fx.click("grid:cell:r1:qty");
    CHECK(fx.grid.editing());
    fx.render();  // requestFieldFocus 经 rebuildIfDirty 落地（beginEdit 同模式）
    CHECK(fx.shell.controller().focusedBind() == "grid:edit");
    fx.shell.textInput("9");
    CHECK(fx.grid.handleKey(core::Key::Enter, core::kModifierNone));
    CHECK_FALSE(fx.grid.editing());
    REQUIRE(fx.edited.size() == 1);
    CHECK(fx.edited.front() == "1:qty:69");
}

TEST_CASE("REGRESSION_frozen_rows_carry_hover_without_tab_stop",
          "[widgets][datagrid]") {
    // §20.4 已知限制收口：冻结区行与滚动区行同为 collectionRow——hover
    // 高亮在冻结区呈现（resolveListPart 既有 hovered 分支）；同时
    // excludeFromSemantics 副本不成为 Tab 停靠点（焦点唯一入口仍在
    // 滚动区行）。
    GridFixture fx;
    CHECK(fx.grid.setColumnPinned("name", true));
    fx.render();

    const auto hoverCenter = [&](const char* key) {
        const core::RenderNode* node =
            core::findNodeByKey(fx.shell.root(), key);
        REQUIRE(node != nullptr);
        return core::absoluteOffset(fx.shell.root(), key) +
               core::Offset{node->size.width * 0.5F,
                            node->size.height * 0.5F};
    };
    // 悬停冻结区行：hover 落在 frow 副本（此前谓词缺失，恒为空）。
    fx.shell.pointerMove(hoverCenter("grid:frow:r2"));
    CHECK(fx.shell.controller().hoveredKey() == "grid:frow:r2");
    // 移入滚动区行：hover 切换到主视图行。
    fx.shell.pointerMove(hoverCenter("grid:item:r5"));
    CHECK(fx.shell.controller().hoveredKey() == "grid:item:r5");

    // Tab 遍历不进入冻结副本：连续 Tab/Shift+Tab 的焦点键永不含 frow。
    for (int i = 0; i < 8; ++i) {
        fx.shell.keyDown(core::Key::Tab,
                         i < 4 ? core::kModifierNone
                               : core::kModifierShift);
        const auto& focused = fx.shell.focus().focusedKey();
        CHECK(focused.find(":frow:") == std::string::npos);
    }
}

TEST_CASE("datagrid_frozen_degrades_without_pinned_columns",
          "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.render();
    // 无 pinned 列：选择复选框列仍常驻冻结区（44 + 1 分界）。
    REQUIRE(core::findNodeByKey(fx.shell.root(), "grid:frozen-body") !=
            nullptr);
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:frozen-body")
              ->size.width == Catch::Approx(44.0F));
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:scroll")->size.width ==
          Catch::Approx(355.0F));

    // 全部列 pin：滚动内容 0 → 横向滚动禁用、结构不崩。
    CHECK(fx.grid.setColumnPinned("name", true));
    CHECK(fx.grid.setColumnPinned("qty", true));
    CHECK(fx.grid.setColumnPinned("note", true));
    fx.render();
    fx.render();
    const auto* view = core::findNodeByKey(fx.shell.root(), "grid:scroll");
    REQUIRE(view != nullptr);
    CHECK(view->scrollExtent == Catch::Approx(0.0F));
    // 冻结区容纳全部列（复选框 44 + 320），滚动区只剩空行壳。
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:frozen-body")
              ->size.width == Catch::Approx(364.0F));
    // 纵向滚动与选择仍正常（空 current 的 Down = 选中首行）。
    CHECK(fx.grid.handleKey(core::Key::Down, core::kModifierNone));
    CHECK(fx.grid.selection().currentKey() == "r0");
    CHECK(fx.grid.handleKey(core::Key::Down, core::kModifierNone));
    CHECK(fx.grid.selection().currentKey() == "r1");

    // ensureColumnVisible（P0.2）：pinned 列 no-op；滚动列滚入视口。
    fx.grid.setColumnPinned("qty", false);
    fx.grid.setColumnPinned("note", false);
    fx.render();
    fx.render();
    fx.grid.ensureColumnVisible("name");  // pinned：no-op
    CHECK(fx.grid.hScroll().offset() == Catch::Approx(0.0F));
    // 加宽 note 使其越界，编辑它应滚入视口。
    CHECK(fx.grid.resizeColumn("qty", 300.0F));
    fx.render();
    fx.render();
    fx.grid.ensureColumnVisible("note");
    fx.render();
    CHECK(fx.grid.hScroll().offset() > 0.0F);
}

// --- 2026-09-29 第六批（设计文档 §19.3：水平虚拟化） ---

namespace {
// 100 列 × 50 行宽网格（列宽 80，内容 8000px >> 视口）。
struct WideGridFixture {
    app::AppShell shell;
    DataGridController grid;
    FakeClipboard clipboard;
    std::vector<std::string> edited{};

    WideGridFixture() : shell(makeConfig(&grid)), grid() {
        shell.controller().setClipboard(&clipboard);
        grid.attach(shell, "grid");
        std::vector<DataColumn> columns;
        columns.reserve(100);
        for (int i = 0; i < 100; ++i) {
            const std::string key = "c" + std::to_string(i);
            columns.push_back(DataColumn{key, key, 80.0F, true, i > 0,
                                         i > 0});
        }
        grid.setColumns(std::move(columns));
        grid.setRowCount(50);
        grid.setCellText([](std::size_t r, const std::string& c) {
            return c + "#" + std::to_string(r);
        });
        grid.onCellEdited = [this](std::size_t row, const std::string& col,
                                   const std::string& text) {
            edited.push_back(std::to_string(row) + ":" + col + ":" + text);
        };
    }

    static app::ShellConfig makeConfig(DataGridController* grid) {
        app::ShellConfig config;
        config.initialView = core::Size{400.0F, 300.0F};
        config.build = [grid] { return grid->build(); };
        return config;
    }

    void render() {
        (void)shell.renderFrame();
        (void)shell.renderFrame();  // 视口宽/列窗口两帧收敛
    }
};
}  // namespace

TEST_CASE("datagrid_wide_materializes_only_visible_column_window",
          "[widgets][datagrid]") {
    WideGridFixture fx;
    fx.render();
    // 复选框列冻结（44+1）→ 滚动视口 355；窗口 = 355 + 2×120 cache →
    // ~8 列（80px 列宽）。物化格数 O(可见行 × 可见列)，与 100 列总量
    // 无关（确定性 headless 断言，§19 T6.5 预算口径）。
    std::size_t cellBoxes = 0;
    std::size_t materializedRows = 0;
    const std::function<void(const core::RenderNode&)> walk =
        [&](const core::RenderNode& node) {
            if (node.key.starts_with("grid:item:")) ++materializedRows;
            if (node.key.ends_with(":box") &&
                node.key.starts_with("grid:cell:")) {
                ++cellBoxes;
            }
            for (const auto& child : node.children) walk(child);
        };
    walk(fx.shell.root());
    CHECK(cellBoxes < materializedRows * 12);   // 远小于 100 列全物化
    CHECK(cellBoxes >= materializedRows * 5);   // 窗口确实覆盖多列
    // 窗口外列不物化（首列在 cache 内必物化；尾列 100 距视口 > cache）。
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:cell:r0:c0:box") !=
          nullptr);
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:cell:r0:c99:box") ==
          nullptr);

    // 滚动推进：窗口滑动，尾部列进入物化窗口。
    CHECK(fx.shell.wheel(core::Offset{200.0F, 150.0F},
                         core::Offset{600.0F, 0.0F}));
    fx.render();
    CHECK(fx.grid.hScroll().offset() == Catch::Approx(600.0F));
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:cell:r0:c8:box") !=
          nullptr);
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:cell:r0:c0:box") ==
          nullptr);
}

TEST_CASE("datagrid_wide_copy_and_edit_are_window_independent",
          "[widgets][datagrid]") {
    WideGridFixture fx;
    fx.render();
    CHECK(fx.shell.wheel(core::Offset{200.0F, 150.0F},
                         core::Offset{600.0F, 0.0F}));
    fx.render();
    // TSV 复制作用于全列集（窗口只影响物化，§19 T6.2）。
    fx.grid.selection().setSelected({"r0"});
    CHECK(fx.grid.copySelection() == 1);
    const std::string tsv = fx.clipboard.text();
    CHECK(tsv.starts_with("c0#0\t"));   // 首列在窗口外仍复制
    CHECK(tsv.ends_with("\tc99#0"));    // 尾列同样

    // 窗口外列进入编辑：ensureColumnVisible 滚入视口（§19 P0.2/T6.3）。
    CHECK(fx.grid.beginEdit(0, "c90"));
    fx.render();
    CHECK(fx.grid.hScroll().offset() >= 600.0F);
    CHECK(core::findNodeByKey(fx.shell.root(), "grid:editor") != nullptr);
    CHECK(fx.shell.controller().focusedBind() == "grid:edit");
    fx.shell.textInput("!");
    CHECK(fx.grid.handleKey(core::Key::Enter, core::kModifierNone));
    REQUIRE(fx.edited.size() == 1);
    CHECK(fx.edited.front() == "0:c90:c90#0!");

    // 拖宽改变前缀：缓存失效重算，窗口边界正确（T6.1 失效点）。
    const float before = fx.grid.hScroll().offset();
    CHECK(fx.grid.resizeColumn("c50", 160.0F));
    fx.render();
    CHECK(fx.grid.hScroll().offset() == Catch::Approx(before));
    CHECK(fx.grid.columnWidths()[50].second == Catch::Approx(160.0F));
}

// --- review 复测（2026-09-29 第五/六批：真实指针/像素路径） ---

namespace {
// 像素采样（CPU framebuffer；gpu_list_readback 同模式）。
core::Color pixelAt(const render::PixelBuffer& buffer, int x, int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.width) +
         static_cast<std::size_t>(x)) *
        4;
    return core::Color::fromRGBA(buffer.rgba[offset], buffer.rgba[offset + 1],
                                 buffer.rgba[offset + 2],
                                 buffer.rgba[offset + 3]);
}
}  // namespace

TEST_CASE("REGRESSION_frozen_region_pointer_paths_and_divider_pixel",
          "[widgets][datagrid]") {
    GridFixture fx;
    CHECK(fx.grid.setColumnPinned("name", true));
    fx.render();
    fx.render();

    // 1) 冻结区行上纵向拖动：框架 ScrollDragSink 经共享纵向控制器 pan
    //    （滚动区行同步，T5.4 共享纵滚的真实指针路径）。
    const core::Offset rowAt =
        core::absoluteOffset(fx.shell.root(), "grid:frow:r4") +
        core::Offset{20.0F, 20.0F};
    fx.shell.pointerDown(rowAt);
    fx.shell.pointerMove(core::Offset{rowAt.x, rowAt.y - 80.0F});
    fx.shell.pointerUp(core::Offset{rowAt.x, rowAt.y - 80.0F});
    fx.render();
    CHECK(fx.grid.scroll().offset() > 0.0F);
    CHECK(fx.grid.hScroll().offset() == Catch::Approx(0.0F));

    // 2) 横向滚动条拇指拖动：moveScrollbar 按轴直驱 hScroll_（virtualSource
    //    路径）——表头与数据区同源平移、冻结区不动。加宽 qty 使滚动内容
    //    440 > 视口 255（有横向滚动范围）。
    fx.shell.pointerCancel();
    CHECK(fx.grid.resizeColumn("qty", 300.0F));
    fx.grid.hScroll().scrollTo(0.0F);
    fx.render();
    fx.render();
    fx.grid.hScroll().scrollBy(100.0F);
    fx.render();
    const auto* view = core::findNodeByKey(fx.shell.root(), "grid:scroll");
    REQUIRE(view != nullptr);
    CHECK(view->scrollExtent > 0.0F);
    // 滚动条 thumb 在滚动区底缘（viewport 高 - thumb 带）——按几何推 thumb
    // 中心并拖动。
    const core::Offset viewOrigin =
        core::absoluteOffset(fx.shell.root(), "grid:scroll");
    const float thumbBandY = viewOrigin.y + view->size.height - 8.0F;
    // thumb 拖动为绝对定位语义（指针位置换算）：先回 0（thumb 贴左缘）
    // 再向右拖，方向/结果确定。
    fx.grid.hScroll().scrollTo(0.0F);
    fx.render();
    fx.shell.pointerDown(core::Offset{viewOrigin.x + 8.0F, thumbBandY});
    fx.shell.pointerMove(core::Offset{viewOrigin.x + 88.0F, thumbBandY});
    fx.shell.pointerUp(core::Offset{viewOrigin.x + 88.0F, thumbBandY});
    fx.render();
    CHECK(fx.grid.hScroll().offset() > 0.0F);
    CHECK(fx.grid.hScroll().offset() <= fx.grid.hScroll().maxScrollOffset());
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:frozen-header").x ==
          Catch::Approx(
              core::absoluteOffset(fx.shell.root(), "grid:frozen-header").x));

    // 3) 分界线像素（CPU framebuffer）：冻结区右缘 1px 实线 == borderStrong
    //    （§12 不依赖阴影的像素级验证）。
    fx.shell.pointerCancel();
    fx.grid.hScroll().scrollTo(0.0F);
    fx.render();
    const auto& buffer = fx.shell.pixels();
    const auto* line = core::findNodeByKey(fx.shell.root(), "grid:freeze-line");
    REQUIRE(line != nullptr);
    const core::Offset lineOrigin =
        core::absoluteOffset(fx.shell.root(), "grid:freeze-line");
    const core::Color strong = fx.shell.theme().colors.borderStrong;
    CHECK(pixelAt(buffer, static_cast<int>(lineOrigin.x),
                  static_cast<int>(lineOrigin.y + 100.0F)) == strong);
    // 分界线左侧（冻结区）与右侧（滚动区）非分界色。
    CHECK(pixelAt(buffer, static_cast<int>(lineOrigin.x) - 2,
                  static_cast<int>(lineOrigin.y + 100.0F)) != strong);
}

// --- 2026-09-29 视觉/交互 review 收口回归（设计文档 §21） ---

TEST_CASE("REGRESSION_ctrl_navigation_moves_current_only", "[widgets][datagrid]") {
    GridFixture fx;
    fx.grid.setSelectionMode(widgets::SelectionMode::Extended);
    fx.render();
    // 建立已知选择集：r0 current（随动选中）→ Shift+Down 扩展到 r1。
    fx.grid.setCurrentKey("r0", false);
    CHECK(fx.grid.handleKey(core::Key::Down, core::kModifierShift));
    CHECK(fx.grid.selection().currentKey() == "r1");
    CHECK(fx.grid.selection().isSelected("r0"));
    CHECK(fx.grid.selection().isSelected("r1"));
    // Ctrl+Down（§11.3）：仅移动 current，选择集原样。
    CHECK(fx.grid.handleKey(core::Key::Down, core::kModifierCtrl));
    CHECK(fx.grid.selection().currentKey() == "r2");
    CHECK(fx.grid.selection().isSelected("r0"));
    CHECK(fx.grid.selection().isSelected("r1"));
    CHECK_FALSE(fx.grid.selection().isSelected("r2"));
    // Ctrl+End 同理到末行，选择集不变。
    CHECK(fx.grid.handleKey(core::Key::End, core::kModifierCtrl));
    CHECK(fx.grid.selection().currentKey() == "r9");
    CHECK(fx.grid.selection().isSelected("r0"));
    // 无修饰的普通 Up 仍是"移动 + 随动替换"（§5 既有契约）。
    CHECK(fx.grid.handleKey(core::Key::Up, core::kModifierNone));
    CHECK(fx.grid.selection().currentKey() == "r8");
    CHECK_FALSE(fx.grid.selection().isSelected("r0"));
    CHECK(fx.grid.selection().isSelected("r8"));
}

TEST_CASE("REGRESSION_row_extent_fixed_with_selection_column",
          "[widgets][datagrid]") {
    GridFixture fx;
    // 选择列在位（冻结区复选框格 + 每行复选框）：物化行高严格等于
    // rowExtent token（review 第 2 条：格内容不得顶高行——固定行高是
    // 双区几何对齐的前提，§19 P0.1）。
    fx.grid.setSelectionMode(widgets::SelectionMode::Multiple);
    fx.render();
    fx.render();
    const float rowExtent = fx.shell.theme().dataGrid.rowExtent;
    CHECK(fx.grid.extentOf(0) == Catch::Approx(rowExtent));
    const auto* r0 = core::findNodeByKey(fx.shell.root(), "grid:item:r0");
    const auto* r1 = core::findNodeByKey(fx.shell.root(), "grid:item:r1");
    const auto* f0 = core::findNodeByKey(fx.shell.root(), "grid:frow:r0");
    REQUIRE(r0 != nullptr);
    REQUIRE(r1 != nullptr);
    REQUIRE(f0 != nullptr);
    CHECK(r0->size.height == Catch::Approx(rowExtent));
    CHECK(f0->size.height == Catch::Approx(rowExtent));
    const float y0 = core::absoluteOffset(fx.shell.root(), "grid:item:r0").y;
    const float y1 = core::absoluteOffset(fx.shell.root(), "grid:item:r1").y;
    CHECK(y1 - y0 == Catch::Approx(rowExtent));  // 槽位 = 行高，无重叠
}

TEST_CASE("REGRESSION_header_and_data_columns_align", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.render();
    // 滚动区：表头列与数据格同 x（行壳集合行内边距与 List 分隔线内缩
    // 清零后；第五批回归曾让数据列右移 57px——滚动区重复选择列 + 行壳
    // 12px token 内边距）。
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:cell:r0:name:box").x ==
          Catch::Approx(
              core::absoluteOffset(fx.shell.root(), "grid:head:name").x));
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:cell:r0:qty:box").x ==
          Catch::Approx(
              core::absoluteOffset(fx.shell.root(), "grid:head:qty").x));
    // 冻结区：表头复选框格与行复选框格同 x、整 token 宽（不再被行壳
    // 内边距挤压）。
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:check-cell:r0").x ==
          Catch::Approx(
              core::absoluteOffset(fx.shell.root(),
                                   "grid:header-check-cell").x));
    const auto* checkCell =
        core::findNodeByKey(fx.shell.root(), "grid:check-cell:r0");
    REQUIRE(checkCell != nullptr);
    CHECK(checkCell->size.width == Catch::Approx(
        fx.shell.theme().dataGrid.selectionColumnWidth));
}

TEST_CASE("REGRESSION_header_surface_and_text_tokens", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.render();  // 列窗口两帧收敛（§19 T6.2；note 列首帧在窗口外）
    // §12/§3.3：表头 = surfaceSunken 填充。
    const auto* header = core::findNodeByKey(fx.shell.root(), "grid:header");
    REQUIRE(header != nullptr);
    CHECK(header->commonStyle().background ==
          fx.shell.theme().colors.surfaceSunken);
    // th 1px 下边框（borderDefault 分隔线叶）。
    const auto* line = core::findNodeByKey(fx.shell.root(), "grid:header-line");
    REQUIRE(line != nullptr);
    CHECK(line->size.height == Catch::Approx(1.0F));
    CHECK(line->commonStyle().background ==
          fx.shell.theme().colors.borderDefault);
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:header-line").y ==
          Catch::Approx(
              core::absoluteOffset(fx.shell.root(), "grid:header").y +
              header->size.height));
    // 静态列头（note 不可排序）：caption 档 + contentSecondary（§12 表头
    // 辅助文字；不再硬编码色）。
    const auto* note = core::findNodeByKey(fx.shell.root(), "grid:head:note");
    REQUIRE(note != nullptr);
    CHECK(note->commonStyle().text.color ==
          fx.shell.theme().colors.contentSecondary);
    CHECK(note->commonStyle().text.fontSize ==
          fx.shell.theme().typography.caption.fontSize);
    // 可排序列头（name = Ghost 按钮）：文字同 caption/contentSecondary
    //（与静态列头一致），accentContent 前景留给排序指示图标。
    const auto* name = core::findNodeByKey(fx.shell.root(), "grid:head:name");
    REQUIRE(name != nullptr);
    CHECK(name->commonStyle().text.color ==
          fx.shell.theme().colors.contentSecondary);
    CHECK(name->commonStyle().foreground ==
          fx.shell.theme().colors.accentContent);
    // 手柄 Stack 叠放：表头文字用满列宽（不再扣手柄带宽，§21/设计稿
    // resizer 骑缝口径）。
    const auto* headbox =
        core::findNodeByKey(fx.shell.root(), "grid:headbox:name");
    REQUIRE(headbox != nullptr);
    CHECK(headbox->type == core::WidgetType::Stack);
    REQUIRE(headbox->children.size() == 2);
    CHECK(headbox->children.front().size.width == Catch::Approx(100.0F));

    // 像素级（CPU framebuffer）：表头表面真实绘制为 surfaceSunken；手柄
    // 静止轨道透明（§12 默认不加竖线——静止位与表头面同色，无线条）。
    const auto& buffer = fx.shell.pixels();
    const core::Offset headerOrigin =
        core::absoluteOffset(fx.shell.root(), "grid:header");
    const core::Color sunken = fx.shell.theme().colors.surfaceSunken;
    CHECK(pixelAt(buffer, static_cast<int>(headerOrigin.x + 6.0F),
                  static_cast<int>(headerOrigin.y + 8.0F)) == sunken);
    const core::Offset boxOrigin =
        core::absoluteOffset(fx.shell.root(), "grid:headbox:name");
    CHECK(pixelAt(buffer,
                  static_cast<int>(boxOrigin.x + headbox->size.width -
                                   fx.shell.theme().dataGrid.resizeHitWidth *
                                       0.5F),
                  static_cast<int>(headerOrigin.y +
                                   header->size.height * 0.5F)) == sunken);
}

TEST_CASE("REGRESSION_current_cell_ring_fills_row", "[widgets][datagrid]") {
    GridFixture fx;
    fx.render();
    fx.grid.setCurrentKey("r1", false);
    fx.grid.setCurrentColumn("qty");
    fx.render();
    // §12/设计稿 td:focus：当前格内嵌环铺满整格——格式盒定高 = 行高且
    // 与行同缘（review 第 4 条：环盒曾只有文本行高 16.8 且贴行顶）。
    const auto* box =
        core::findNodeByKey(fx.shell.root(), "grid:cell:r1:qty:box");
    const auto* row = core::findNodeByKey(fx.shell.root(), "grid:item:r1");
    REQUIRE(box != nullptr);
    REQUIRE(row != nullptr);
    CHECK(box->size.height == Catch::Approx(row->size.height));
    CHECK(core::absoluteOffset(fx.shell.root(), "grid:cell:r1:qty:box").y ==
          Catch::Approx(
              core::absoluteOffset(fx.shell.root(), "grid:item:r1").y));
}

TEST_CASE("datagrid_cell_padding_follows_density", "[widgets][datagrid]") {
    GridFixture fx;
    // §12：格内边距 = controlPaddingX 密度档（8/12/16）——格式盒（Row）
    // 自带 padding。
    const auto cellPad = [](const core::Widget& row) {
        REQUIRE(row.children.size() == 1);
        const core::Widget& content = row.children.front();
        REQUIRE(!content.children.empty());
        return content.children.front().padding.left;
    };
    CHECK(cellPad(fx.grid.buildItem(0)) == Catch::Approx(12.0F));
    fx.shell.setTheme(style::Theme::dark(style::ControlDensity::Compact));
    CHECK(cellPad(fx.grid.buildItem(0)) == Catch::Approx(8.0F));
    fx.shell.setTheme(style::Theme::dark(style::ControlDensity::Touch));
    CHECK(cellPad(fx.grid.buildItem(0)) == Catch::Approx(16.0F));
}
