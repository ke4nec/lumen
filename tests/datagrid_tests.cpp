// M14-D：DataGrid 契约测试（docs/lumen-datagrid-design.md §8）。
//
// 覆盖首版契约切片：列模型与列宽调整、行虚拟化物化、选择（共享
// SelectionModel）、键盘导航与列焦点、排序回调与状态、TSV 复制粘贴、
// 单元格编辑与校验。水平虚拟化/RTL/拖放为后续增量（不在本文件断言）。

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
            sortedBy.push_back(col + (ascending ? "+" : "-"));
        };
        grid.onRowsPasted = [this](const std::vector<std::vector<std::string>>& rows) {
            pasted = rows;
        };
        grid.onCellEdited = [this](std::size_t row, const std::string& col,
                                   const std::string& text) {
            edited.push_back(std::to_string(row) + ":" + col + ":" + text);
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
    const std::function<void(const core::RenderNode&)> walk =
        [&](const core::RenderNode& node) {
            if (node.key.starts_with("grid:item:")) ++rows;
            if (node.type == core::WidgetType::Text) ++texts;
            for (const auto& child : node.children) walk(child);
        };
    walk(fx.shell.root());
    CHECK(rows < 100);
    CHECK(rows >= 5);
    // 每物化行 3 个单元格文本 + 表头 3 个。
    CHECK(texts >= (rows + 1) * 3 - 1);
    // 表头排序指示器初始为空。
    const auto* header = core::findNodeByKey(fx.shell.root(), "grid:header");
    REQUIRE(header != nullptr);
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

TEST_CASE("datagrid_sort_request_toggles_state_and_callback", "[widgets][datagrid]") {
    GridFixture fx;
    fx.grid.requestSort("name");
    CHECK(fx.grid.sortColumn() == "name");
    CHECK(fx.grid.sortAscending());
    REQUIRE(fx.sortedBy.size() == 1);
    CHECK(fx.sortedBy.front() == "name+");
    fx.grid.requestSort("name");
    CHECK_FALSE(fx.grid.sortAscending());
    CHECK(fx.sortedBy.back() == "name-");
    // 不可排序列拒绝。
    fx.grid.requestSort("note");
    CHECK(fx.grid.sortColumn() == "name");  // 未变
    CHECK(fx.sortedBy.size() == 2);
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
