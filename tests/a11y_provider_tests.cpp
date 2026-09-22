// M13 原生无障碍 provider（docs/lumen-accessibility-provider-design.md）
// 测试：工厂按编译事实分流（未编入安全降级）、Fake host 能力如实上报、
// Windows UIA provider 的 COM 直驱断言（fragment 导航/属性映射/pattern
// 回灌/事件/运行时 id 稳定/重入安全）——全部 headless，不依赖真实 AT。
//
// 命名遵循项目测试规范（行为命名，*_tests.cpp）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "lumen/accessibility/bridge.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/platform/fake_host.h"

#if defined(__linux__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_ATSPI)
#include "atspi_provider.h"
#endif

#if defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)
// SDK 头的 min/max 宏会破坏 core/geometry.h（numeric_limits）。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <UIAutomation.h>
#include <windows.h>

#include <wrl/client.h>

#include "uia_provider.h"
#endif

using namespace lumen;

#if defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)
using accessibility::SemanticsDiff;
using accessibility::SemanticsNode;
using accessibility::SemanticsRole;
using accessibility::SemanticsTree;
using Microsoft::WRL::ComPtr;
#endif

namespace {

// 工厂/能力断言（全平台）：编入与否都必须如实报告。
TEST_CASE("a11y_factory_reports_compiled_provider_honestly", "[a11y]") {
    const std::string name = accessibility::accessibilityProviderName();
    accessibility::PlatformAccessibilityHost host;
    std::string diagnostics;
    auto bridge = accessibility::createPlatformAccessibilityBridge(host,
                                                                   &diagnostics);
    if (name.empty()) {
        // 未编入：安全降级 + 可读原因（plan §2.3 不变量）。
        REQUIRE(bridge == nullptr);
        CHECK_FALSE(diagnostics.empty());
        return;
    }
    // 编入：headless（无原生窗口）桥可用，名称与编译事实一致。
    REQUIRE(bridge != nullptr);
    CHECK(bridge->bridgeName() == name);
    if (name == "atspi" && !bridge->available()) {
        // A headless CI process may have libdbus but no desktop accessibility
        // bus.  The provider remains compiled and safe, while capability is
        // correctly reported as unavailable until a real session is present.
        CHECK_FALSE(diagnostics.empty());
        return;
    }
    CHECK(bridge->available());
}

TEST_CASE("fake_host_reports_accessibility_capability_on_note", "[a11y]") {
    platform::FakeApplicationHost host;
    CHECK_FALSE(host.capabilities().accessibility);
    host.noteAccessibilityBridgeActive(true);
    CHECK(host.capabilities().accessibility);
    host.noteAccessibilityBridgeActive(false);
    CHECK_FALSE(host.capabilities().accessibility);
}

#if defined(__linux__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_ATSPI)

TEST_CASE("atspi_provider_keeps_semantic_paths_stable", "[a11y][atspi]") {
    accessibility::PlatformAccessibilityHost host;
    std::string diagnostics;
    accessibility::atspi::AtspiAccessibilityBridge bridge(host, &diagnostics);

    accessibility::SemanticsTree tree;
    tree.rootId = "window";
    accessibility::SemanticsNode root;
    root.id = tree.rootId;
    root.role = accessibility::SemanticsRole::Window;
    root.children = {"button"};
    tree.nodes.emplace(root.id, root);
    accessibility::SemanticsNode button;
    button.id = "button";
    button.role = accessibility::SemanticsRole::Button;
    button.label = "Apply";
    tree.nodes.emplace(button.id, button);

    accessibility::SemanticsDiff diff;
    diff.added = {"window", "button"};
    bridge.updateTree(tree, diff, {});

    CHECK(bridge.nodeCountForTesting() == 2);
    const std::string firstPath = bridge.objectPathForTesting("button");
    CHECK(firstPath == bridge.objectPathForTesting("button"));
    CHECK(firstPath.find("/org/a11y/atspi/accessible/") == 0);

    tree.nodes.at("button").label = "Apply now";
    diff = {};
    diff.changed = {"button"};
    bridge.updateTree(tree, diff, {});
    CHECK(firstPath == bridge.objectPathForTesting("button"));
}

#endif

#if defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)

struct DispatchCall {
    std::string nodeId;
    std::uint32_t action{0};
    std::string value{};
    float scrollDeltaY{0.0F};
};

class RecordingSink final : public accessibility::uia::UiaEventSink {
  public:
    void raise(const accessibility::uia::UiaEvent& event) override {
        events.push_back(event);
    }
    std::vector<accessibility::uia::UiaEvent> events;
};

std::string bstrToUtf8(BSTR value) {
    if (value == nullptr) {
        return {};
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                          nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(count - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8.data(), count, nullptr,
                        nullptr);
    return utf8;
}

std::string propertyName(IUnknown* unknown) {
    ComPtr<IRawElementProviderSimple> simple;
    REQUIRE(unknown->QueryInterface(IID_PPV_ARGS(&simple)) == S_OK);
    VARIANT var;
    VariantInit(&var);
    REQUIRE(simple->GetPropertyValue(UIA_NamePropertyId, &var) == S_OK);
    const std::string name = var.vt == VT_BSTR ? bstrToUtf8(var.bstrVal) : "";
    VariantClear(&var);
    return name;
}

LONG propertyControlType(IUnknown* unknown) {
    ComPtr<IRawElementProviderSimple> simple;
    REQUIRE(unknown->QueryInterface(IID_PPV_ARGS(&simple)) == S_OK);
    VARIANT var;
    VariantInit(&var);
    REQUIRE(simple->GetPropertyValue(UIA_ControlTypePropertyId, &var) == S_OK);
    const LONG type = var.vt == VT_I4 ? var.lVal : 0;
    VariantClear(&var);
    return type;
}

bool propertyBool(IUnknown* unknown, PROPERTYID id) {
    ComPtr<IRawElementProviderSimple> simple;
    REQUIRE(unknown->QueryInterface(IID_PPV_ARGS(&simple)) == S_OK);
    VARIANT var;
    VariantInit(&var);
    REQUIRE(simple->GetPropertyValue(id, &var) == S_OK);
    const bool value = var.vt == VT_BOOL && var.boolVal == VARIANT_TRUE;
    VariantClear(&var);
    return value;
}

// 语义树 fixture：Window → Group{OK, Cancel(禁用), 名字字段(焦点),
// 音量滑条, 同意复选}。
SemanticsTree makeTree() {
    SemanticsTree tree;
    tree.rootId = "root";

    SemanticsNode root;
    root.id = "root";
    root.role = SemanticsRole::Window;
    root.label = "Demo";
    root.children = {"group"};
    tree.nodes["root"] = root;

    SemanticsNode group;
    group.id = "group";
    group.role = SemanticsRole::Group;
    group.bounds = core::Rect::fromXYWH(0, 0, 400, 200);
    group.children = {"btn-ok", "btn-cancel", "name-field", "volume-slider",
                      "agree-check"};
    tree.nodes["group"] = group;

    SemanticsNode ok;
    ok.id = "btn-ok";
    ok.role = SemanticsRole::Button;
    ok.label = "OK";
    ok.actions = accessibility::kActionFocus | accessibility::kActionActivate;
    ok.bounds = core::Rect::fromXYWH(0, 0, 80, 32);
    tree.nodes["btn-ok"] = ok;

    SemanticsNode cancel;
    cancel.id = "btn-cancel";
    cancel.role = SemanticsRole::Button;
    cancel.label = "Cancel";
    cancel.actions = accessibility::kActionFocus |
                      accessibility::kActionActivate;
    cancel.flags = 0;  // 禁用（enabled 位清除）。
    cancel.bounds = core::Rect::fromXYWH(96, 0, 80, 32);
    tree.nodes["btn-cancel"] = cancel;

    SemanticsNode field;
    field.id = "name-field";
    field.role = SemanticsRole::TextField;
    field.label = "Name";
    field.value = "hello";
    field.actions = accessibility::kActionFocus |
                     accessibility::kActionSetValue;
    field.bounds = core::Rect::fromXYWH(0, 48, 200, 32);
    tree.nodes["name-field"] = field;

    SemanticsNode slider;
    slider.id = "volume-slider";
    slider.role = SemanticsRole::Slider;
    slider.label = "Volume";
    slider.value = "40";
    slider.actions = accessibility::kActionFocus |
                      accessibility::kActionSetValue;
    slider.bounds = core::Rect::fromXYWH(0, 96, 200, 24);
    tree.nodes["volume-slider"] = slider;

    SemanticsNode check;
    check.id = "agree-check";
    check.role = SemanticsRole::Checkbox;
    check.label = "Agree";
    check.actions = accessibility::kActionFocus |
                     accessibility::kActionActivate;
    check.bounds = core::Rect::fromXYWH(0, 136, 120, 24);
    tree.nodes["agree-check"] = check;
    return tree;
}

SemanticsDiff fullDiffOf(const SemanticsTree& tree) {
    SemanticsDiff diff;
    for (const auto& [id, node] : tree.nodes) {
        (void)node;
        diff.added.push_back(id);
    }
    return diff;
}

accessibility::PlatformAccessibilityHost makeHost(
    std::vector<DispatchCall>* calls, float deviceScale = 1.0F) {
    accessibility::PlatformAccessibilityHost host;
    host.deviceScale = deviceScale;
    host.applicationName = "demo";
    host.dispatch = [calls](const std::string& nodeId, std::uint32_t action,
                            const std::string& value, float scrollDeltaY) {
        calls->push_back(DispatchCall{nodeId, action, value, scrollDeltaY});
        return accessibility::SemanticsActionStatus::Handled;
    };
    return host;
}

TEST_CASE("uia_fragment_navigation_follows_semantics_order", "[a11y]") {
    std::vector<DispatchCall> calls;
    accessibility::uia::UiaAccessibilityBridge bridge(makeHost(&calls),
                                                      nullptr);
    const SemanticsTree tree = makeTree();
    bridge.updateTree(tree, fullDiffOf(tree), "");

    ComPtr<IRawElementProviderFragmentRoot> root;
    root.Attach(bridge.rootProviderForTesting());
    REQUIRE(root != nullptr);
    // FragmentRoot 与 Fragment 平行接口：导航经 QI 到 Fragment。
    ComPtr<IRawElementProviderFragment> rootFragment;
    REQUIRE(root->QueryInterface(IID_PPV_ARGS(&rootFragment)) == S_OK);

    // 根 → 首子 = group；group 首子 = btn-ok；顺序兄弟遍历。
    ComPtr<IRawElementProviderFragment> group;
    REQUIRE(rootFragment->Navigate(NavigateDirection_FirstChild, &group) ==
            S_OK);
    REQUIRE(group != nullptr);
    ComPtr<IRawElementProviderFragment> first;
    REQUIRE(group->Navigate(NavigateDirection_FirstChild, &first) == S_OK);
    REQUIRE(first != nullptr);
    CHECK(propertyName(first.Get()) == "OK");
    ComPtr<IRawElementProviderFragment> next;
    REQUIRE(first->Navigate(NavigateDirection_NextSibling, &next) == S_OK);
    REQUIRE(next != nullptr);
    CHECK(propertyName(next.Get()) == "Cancel");
    ComPtr<IRawElementProviderFragment> last;
    REQUIRE(group->Navigate(NavigateDirection_LastChild, &last) == S_OK);
    CHECK(propertyName(last.Get()) == "Agree");
    // 父导航回到同一 provider 实例（缓存复用）。
    ComPtr<IRawElementProviderFragment> parent;
    REQUIRE(next->Navigate(NavigateDirection_Parent, &parent) == S_OK);
    CHECK(parent == group);
    // 根无父。
    ComPtr<IRawElementProviderFragment> none;
    REQUIRE(rootFragment->Navigate(NavigateDirection_Parent, &none) == S_OK);
    CHECK(none == nullptr);
}

TEST_CASE("uia_properties_map_role_flags_and_bounds", "[a11y]") {
    std::vector<DispatchCall> calls;
    accessibility::uia::UiaAccessibilityBridge bridge(
        makeHost(&calls, 2.0F), nullptr);
    const SemanticsTree tree = makeTree();
    bridge.updateTree(tree, fullDiffOf(tree), "name-field");

    ComPtr<IRawElementProviderFragment> ok =
        bridge.fragmentForTesting("btn-ok");
    ComPtr<IRawElementProviderFragment> cancel =
        bridge.fragmentForTesting("btn-cancel");
    ComPtr<IRawElementProviderFragment> field =
        bridge.fragmentForTesting("name-field");
    ComPtr<IRawElementProviderFragment> slider =
        bridge.fragmentForTesting("volume-slider");
    REQUIRE(ok != nullptr);
    REQUIRE(cancel != nullptr);
    REQUIRE(field != nullptr);
    REQUIRE(slider != nullptr);

    CHECK(propertyControlType(ok.Get()) == UIA_ButtonControlTypeId);
    CHECK(propertyControlType(field.Get()) == UIA_EditControlTypeId);
    CHECK(propertyControlType(slider.Get()) == UIA_SliderControlTypeId);
    CHECK(propertyBool(ok.Get(), UIA_IsEnabledPropertyId));
    CHECK_FALSE(propertyBool(cancel.Get(), UIA_IsEnabledPropertyId));
    CHECK(propertyBool(ok.Get(), UIA_IsKeyboardFocusablePropertyId));
    CHECK_FALSE(propertyBool(cancel.Get(), UIA_IsKeyboardFocusablePropertyId));
    CHECK(propertyBool(field.Get(), UIA_HasKeyboardFocusPropertyId));
    // headless（无 HWND）：快照倍率参与 bounds（2.0 → 80×2）。
    UiaRect rect{};
    REQUIRE(ok->get_BoundingRectangle(&rect) == S_OK);
    CHECK(rect.left == Catch::Approx(0.0));
    CHECK(rect.width == Catch::Approx(160.0));
    CHECK(rect.height == Catch::Approx(64.0));
    // 根固定 Pane（HWND 提供窗口框架元素）+ 空标签时应用名兜底。
    ComPtr<IRawElementProviderFragmentRoot> root;
    root.Attach(bridge.rootProviderForTesting());
    CHECK(propertyControlType(root.Get()) == UIA_PaneControlTypeId);
    CHECK(propertyName(root.Get()) == "Demo");
}

TEST_CASE("uia_invoke_pattern_dispatches_activate_on_ui_thread", "[a11y]") {
    std::vector<DispatchCall> calls;
    accessibility::uia::UiaAccessibilityBridge bridge(makeHost(&calls),
                                                      nullptr);
    const SemanticsTree tree = makeTree();
    bridge.updateTree(tree, fullDiffOf(tree), "");

    ComPtr<IRawElementProviderFragment> ok =
        bridge.fragmentForTesting("btn-ok");
    ComPtr<IRawElementProviderSimple> simple;
    REQUIRE(ok->QueryInterface(IID_PPV_ARGS(&simple)) == S_OK);
    ComPtr<IUnknown> pattern;
    REQUIRE(simple->GetPatternProvider(UIA_InvokePatternId, &pattern) == S_OK);
    REQUIRE(pattern != nullptr);
    ComPtr<IInvokeProvider> invoke;
    REQUIRE(pattern->QueryInterface(IID_PPV_ARGS(&invoke)) == S_OK);
    REQUIRE(invoke->Invoke() == S_OK);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].nodeId == "btn-ok");
    CHECK(calls[0].action == accessibility::kActionActivate);

    // 无 Activate 声明的节点（Group）不提供 Invoke。
    ComPtr<IRawElementProviderFragment> group =
        bridge.fragmentForTesting("group");
    ComPtr<IRawElementProviderSimple> groupSimple;
    REQUIRE(group->QueryInterface(IID_PPV_ARGS(&groupSimple)) == S_OK);
    ComPtr<IUnknown> groupPattern;
    REQUIRE(groupSimple->GetPatternProvider(UIA_InvokePatternId,
                                            &groupPattern) == S_OK);
    CHECK(groupPattern == nullptr);
}

TEST_CASE("uia_toggle_value_and_range_patterns_roundtrip", "[a11y]") {
    std::vector<DispatchCall> calls;
    accessibility::uia::UiaAccessibilityBridge bridge(makeHost(&calls),
                                                      nullptr);
    SemanticsTree tree = makeTree();
    bridge.updateTree(tree, fullDiffOf(tree), "");

    // 复选：ToggleState 读语义 checked 位；Toggle 走 Activate 同路径。
    ComPtr<IRawElementProviderFragment> check =
        bridge.fragmentForTesting("agree-check");
    ComPtr<IRawElementProviderSimple> checkSimple;
    REQUIRE(check->QueryInterface(IID_PPV_ARGS(&checkSimple)) == S_OK);
    ComPtr<IUnknown> togglePattern;
    REQUIRE(checkSimple->GetPatternProvider(UIA_TogglePatternId,
                                            &togglePattern) == S_OK);
    ComPtr<IToggleProvider> toggle;
    REQUIRE(togglePattern->QueryInterface(IID_PPV_ARGS(&toggle)) == S_OK);
    ToggleState state{};
    REQUIRE(toggle->get_ToggleState(&state) == S_OK);
    CHECK(state == ToggleState_Off);
    REQUIRE(toggle->Toggle() == S_OK);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].action == accessibility::kActionActivate);
    // checked 置位后状态翻转（应用侧回写经新树推送）。
    tree.nodes["agree-check"].flags |= accessibility::kSemanticsChecked;
    SemanticsDiff diff;
    diff.changed.push_back("agree-check");
    bridge.updateTree(tree, diff, "");
    REQUIRE(toggle->get_ToggleState(&state) == S_OK);
    CHECK(state == ToggleState_On);

    // 文本字段：值读取 + SetValue 回灌字符串。
    ComPtr<IRawElementProviderFragment> field =
        bridge.fragmentForTesting("name-field");
    ComPtr<IRawElementProviderSimple> fieldSimple;
    REQUIRE(field->QueryInterface(IID_PPV_ARGS(&fieldSimple)) == S_OK);
    ComPtr<IUnknown> valuePattern;
    REQUIRE(fieldSimple->GetPatternProvider(UIA_ValuePatternId,
                                            &valuePattern) == S_OK);
    ComPtr<IValueProvider> value;
    REQUIRE(valuePattern->QueryInterface(IID_PPV_ARGS(&value)) == S_OK);
    BSTR raw{};
    REQUIRE(value->get_Value(&raw) == S_OK);
    CHECK(bstrToUtf8(raw) == "hello");
    SysFreeString(raw);
    BOOL readOnly{FALSE};
    REQUIRE(value->get_IsReadOnly(&readOnly) == S_OK);
    CHECK(readOnly == FALSE);
    REQUIRE(value->SetValue(L"changed") == S_OK);
    REQUIRE(calls.size() == 2);
    CHECK(calls[1].nodeId == "name-field");
    CHECK(calls[1].action == accessibility::kActionSetValue);
    CHECK(calls[1].value == "changed");

    // 滑条：0..100 百分比契约；SetValue 以可解析字符串回灌。
    ComPtr<IRawElementProviderFragment> slider =
        bridge.fragmentForTesting("volume-slider");
    ComPtr<IRawElementProviderSimple> sliderSimple;
    REQUIRE(slider->QueryInterface(IID_PPV_ARGS(&sliderSimple)) == S_OK);
    ComPtr<IUnknown> rangePattern;
    REQUIRE(sliderSimple->GetPatternProvider(UIA_RangeValuePatternId,
                                             &rangePattern) == S_OK);
    ComPtr<IRangeValueProvider> range;
    REQUIRE(rangePattern->QueryInterface(IID_PPV_ARGS(&range)) == S_OK);
    double current{0};
    double minimum{0};
    double maximum{0};
    REQUIRE(range->get_Value(&current) == S_OK);
    REQUIRE(range->get_Minimum(&minimum) == S_OK);
    REQUIRE(range->get_Maximum(&maximum) == S_OK);
    CHECK(current == Catch::Approx(40.0));
    CHECK(minimum == Catch::Approx(0.0));
    CHECK(maximum == Catch::Approx(100.0));
    REQUIRE(range->SetValue(55.0) == S_OK);
    REQUIRE(calls.size() == 3);
    CHECK(calls[2].action == accessibility::kActionSetValue);
    CHECK(calls[2].value == "55.000");
}

TEST_CASE("uia_events_follow_tree_diffs_and_focus", "[a11y]") {
    std::vector<DispatchCall> calls;
    accessibility::uia::UiaAccessibilityBridge bridge(makeHost(&calls),
                                                      nullptr);
    RecordingSink sink;
    bridge.setEventSinkForTesting(&sink);

    SemanticsTree tree = makeTree();
    bridge.updateTree(tree, fullDiffOf(tree), "");
    // 首次全量：一次整体结构失效。
    REQUIRE(sink.events.size() == 1);
    CHECK(sink.events[0].kind ==
          accessibility::uia::UiaEvent::Kind::StructureChanged);

    tree.nodes["btn-ok"].label = "Sure";
    SemanticsDiff diff;
    diff.changed.push_back("btn-ok");
    bridge.updateTree(tree, diff, "");
    REQUIRE(sink.events.size() == 2);
    CHECK(sink.events[1].kind ==
          accessibility::uia::UiaEvent::Kind::PropertyChanged);
    CHECK(sink.events[1].nodeId == "btn-ok");
    CHECK(sink.events[1].propertyId == static_cast<int>(UIA_NamePropertyId));

    // 勾选位翻转 → ToggleState 属性事件。
    tree.nodes["agree-check"].flags |= accessibility::kSemanticsChecked;
    diff = SemanticsDiff{};
    diff.changed.push_back("agree-check");
    bridge.updateTree(tree, diff, "");
    REQUIRE(sink.events.size() == 3);
    CHECK(sink.events[2].propertyId ==
          static_cast<int>(UIA_ToggleToggleStatePropertyId));

    // 焦点变化事件。
    bridge.setFocusedNode("name-field");
    REQUIRE(sink.events.size() == 4);
    CHECK(sink.events[3].kind ==
          accessibility::uia::UiaEvent::Kind::FocusChanged);
    CHECK(sink.events[3].nodeId == "name-field");
    // 根 GetFocus 与语义焦点一致。
    ComPtr<IRawElementProviderFragmentRoot> root;
    root.Attach(bridge.rootProviderForTesting());
    ComPtr<IRawElementProviderFragment> focused;
    REQUIRE(root->GetFocus(&focused) == S_OK);
    REQUIRE(focused != nullptr);
    CHECK(propertyName(focused.Get()) == "Name");
}

TEST_CASE("uia_runtime_id_and_provider_stable_across_updates", "[a11y]") {
    std::vector<DispatchCall> calls;
    accessibility::uia::UiaAccessibilityBridge bridge(makeHost(&calls),
                                                      nullptr);
    SemanticsTree tree = makeTree();
    bridge.updateTree(tree, fullDiffOf(tree), "");

    ComPtr<IRawElementProviderFragment> first =
        bridge.fragmentForTesting("btn-ok");
    REQUIRE(first != nullptr);
    SAFEARRAY* idBefore{nullptr};
    REQUIRE(first->GetRuntimeId(&idBefore) == S_OK);
    LONG valueBefore[3] = {0, 0, 0};
    {
        LONG* item = nullptr;
        SafeArrayAccessData(idBefore, reinterpret_cast<void**>(&item));
        for (LONG index = 0; index < 3; ++index) {
            valueBefore[index] = item[index];
        }
        SafeArrayUnaccessData(idBefore);
    }

    // 增量更新（bounds 变化）后：同 identity → 同 provider 实例 + 同
    // RuntimeId（AT 焦点不漂移）。
    tree.nodes["btn-ok"].bounds = core::Rect::fromXYWH(8, 8, 80, 32);
    SemanticsDiff diff;
    diff.changed.push_back("btn-ok");
    bridge.updateTree(tree, diff, "");

    ComPtr<IRawElementProviderFragment> second =
        bridge.fragmentForTesting("btn-ok");
    CHECK(second == first);
    SAFEARRAY* idAfter{nullptr};
    REQUIRE(second->GetRuntimeId(&idAfter) == S_OK);
    LONG valueAfter[3] = {0, 0, 0};
    {
        LONG* item = nullptr;
        SafeArrayAccessData(idAfter, reinterpret_cast<void**>(&item));
        for (LONG index = 0; index < 3; ++index) {
            valueAfter[index] = item[index];
        }
        SafeArrayUnaccessData(idAfter);
    }
    CHECK(valueAfter[0] == valueBefore[0]);
    CHECK(valueAfter[1] == valueBefore[1]);
    CHECK(valueAfter[2] == valueBefore[2]);
    SafeArrayDestroy(idBefore);
    SafeArrayDestroy(idAfter);

    // 移除后表内不可达。
    tree.nodes.erase("btn-ok");
    tree.nodes["group"].children.erase(
        tree.nodes["group"].children.begin());
    SemanticsDiff removed;
    removed.removed.push_back("btn-ok");
    bridge.updateTree(tree, removed, "");
    CHECK_FALSE(bridge.hasNode("btn-ok"));
    CHECK(bridge.fragmentForTesting("btn-ok") == nullptr);
}

TEST_CASE("uia_root_hit_test_returns_deepest_node", "[a11y]") {
    std::vector<DispatchCall> calls;
    accessibility::uia::UiaAccessibilityBridge bridge(
        makeHost(&calls, 1.0F), nullptr);
    const SemanticsTree tree = makeTree();
    bridge.updateTree(tree, fullDiffOf(tree), "");

    ComPtr<IRawElementProviderFragmentRoot> root;
    root.Attach(bridge.rootProviderForTesting());
    ComPtr<IRawElementProviderFragment> rootFragment;
    REQUIRE(root->QueryInterface(IID_PPV_ARGS(&rootFragment)) == S_OK);
    // (10, 5) 落在 OK 按钮内（headless 无 HWND：物理=逻辑）。
    ComPtr<IRawElementProviderFragment> hit;
    REQUIRE(root->ElementProviderFromPoint(10.0, 5.0, &hit) == S_OK);
    REQUIRE(hit != nullptr);
    CHECK(propertyName(hit.Get()) == "OK");
    // 空白点归属根。
    ComPtr<IRawElementProviderFragment> fallback;
    REQUIRE(root->ElementProviderFromPoint(1000.0, 1000.0, &fallback) == S_OK);
    REQUIRE(fallback != nullptr);
    CHECK(fallback == rootFragment);
}

TEST_CASE("uia_dispatch_reentrancy_keeps_identity_copies", "[a11y]") {
    std::vector<DispatchCall> calls;
    SemanticsTree tree = makeTree();
    accessibility::uia::UiaAccessibilityBridge* bridgePtr = nullptr;
    accessibility::PlatformAccessibilityHost host;
    host.deviceScale = 1.0F;
    // dispatch 内同步推送移除自身节点的新树（模拟 handler 触发的重建
    // 期间语义推送——重入顺序由 UI 线程单线程保证）。
    host.dispatch = [&calls, &tree,
                     &bridgePtr](const std::string& nodeId,
                                 std::uint32_t action,
                                 const std::string& value, float) {
        calls.push_back(DispatchCall{nodeId, action, value, 0.0F});
        if (nodeId == "btn-ok" && calls.size() == 1 && bridgePtr != nullptr) {
            SemanticsTree next = tree;
            next.nodes.erase("btn-ok");
            next.nodes["group"].children.erase(
                next.nodes["group"].children.begin());
            SemanticsDiff diff;
            diff.removed.push_back("btn-ok");
            tree = next;
            bridgePtr->updateTree(next, diff, "");
        }
        return accessibility::SemanticsActionStatus::Handled;
    };
    accessibility::uia::UiaAccessibilityBridge bridge(host, nullptr);
    bridgePtr = &bridge;
    bridge.updateTree(tree, fullDiffOf(tree), "");

    ComPtr<IRawElementProviderFragment> ok =
        bridge.fragmentForTesting("btn-ok");
    ComPtr<IRawElementProviderSimple> simple;
    REQUIRE(ok->QueryInterface(IID_PPV_ARGS(&simple)) == S_OK);
    ComPtr<IUnknown> pattern;
    REQUIRE(simple->GetPatternProvider(UIA_InvokePatternId, &pattern) == S_OK);
    ComPtr<IInvokeProvider> invoke;
    REQUIRE(pattern->QueryInterface(IID_PPV_ARGS(&invoke)) == S_OK);
    REQUIRE(invoke->Invoke() == S_OK);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].nodeId == "btn-ok");
    // 节点已移除：残留引用上的再次 Invoke 仍以拷贝 id 安全分发。
    REQUIRE(invoke->Invoke() == S_OK);
    REQUIRE(calls.size() == 2);
    CHECK(calls[1].nodeId == "btn-ok");
    // 已移除节点不再提供 pattern。
    ComPtr<IUnknown> stale;
    CHECK(FAILED(simple->GetPatternProvider(UIA_InvokePatternId, &stale)));
}

// 真实窗口端到端（窗口 smoke 类）：真实 HWND + UIA 客户端 API
//（CUIAutomation::ElementFromHandle）走完整链路——WM_GETOBJECT 子类应答
// → fragment 树 → Invoke pattern → dispatch 回灌。不依赖屏幕阅读器；
// 默认跳过（headless/无桌面环境），设 LUMEN_UIA_LIVE_SMOKE=1 启用
//（本地 Windows 与 CI 桌面环境）。
TEST_CASE("uia_live_window_end_to_end_via_uia_client", "[a11y][live]") {
    if (std::getenv("LUMEN_UIA_LIVE_SMOKE") == nullptr) {
        return;
    }
    struct ComInit {
        HRESULT hr;
        explicit ComInit()
            : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
        ~ComInit() {
            if (SUCCEEDED(hr)) {
                CoUninitialize();
            }
        }
    } comInit;
    REQUIRE(SUCCEEDED(comInit.hr));

    // 简单 Win32 窗口（UIA core 对隐藏窗口也能应答 WM_GETOBJECT）。
    struct WindowGuard {
        HWND hwnd{nullptr};
        static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        WindowGuard() {
            const wchar_t className[] = L"LumenUiaSmoke";
            WNDCLASSW wc{};
            wc.lpfnWndProc = proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = className;
            RegisterClassW(&wc);
            hwnd = CreateWindowExW(0, className, L"Lumen UIA smoke",
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                   CW_USEDEFAULT, 400, 300, nullptr, nullptr,
                                   wc.hInstance, nullptr);
        }
        ~WindowGuard() {
            if (hwnd != nullptr) {
                DestroyWindow(hwnd);
            }
        }
    } window;
    REQUIRE(window.hwnd != nullptr);

    std::vector<DispatchCall> calls;
    accessibility::PlatformAccessibilityHost host = makeHost(&calls);
    host.nativeWindow = window.hwnd;
    accessibility::uia::UiaAccessibilityBridge bridge(host, nullptr);
    REQUIRE(bridge.available());
    const SemanticsTree tree = makeTree();
    bridge.updateTree(tree, fullDiffOf(tree), "");

    // UIA 客户端：从 HWND 取元素树，找 "OK" 按钮并 Invoke。
    ComPtr<IUIAutomation> automation;
    REQUIRE(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&automation)) == S_OK);
    ComPtr<IUIAutomationElement> root;
    REQUIRE(automation->ElementFromHandle(window.hwnd, &root) == S_OK);
    REQUIRE(root != nullptr);

    VARIANT nameVar;
    VariantInit(&nameVar);
    nameVar.vt = VT_BSTR;
    nameVar.bstrVal = SysAllocString(L"OK");
    ComPtr<IUIAutomationCondition> condition;
    REQUIRE(automation->CreatePropertyCondition(
                UIA_NamePropertyId, nameVar, &condition) == S_OK);
    VariantClear(&nameVar);

    ComPtr<IUIAutomationElement> okButton;
    REQUIRE(root->FindFirst(TreeScope_Descendants, condition.Get(),
                            &okButton) == S_OK);
    REQUIRE(okButton != nullptr);
    BSTR rawName{nullptr};
    REQUIRE(okButton->get_CurrentName(&rawName) == S_OK);
    CHECK(bstrToUtf8(rawName) == "OK");
    SysFreeString(rawName);

    ComPtr<IUIAutomationInvokePattern> invoke;
    REQUIRE(okButton->GetCurrentPatternAs(UIA_InvokePatternId,
                                          IID_PPV_ARGS(&invoke)) == S_OK);
    // 这里可能为 null：UIA 客户端侧 pattern 经代理层。为 null 时直接走
    // provider 侧断言（fragmentForTesting 路径已覆盖），仅记录。
    if (invoke != nullptr) {
        REQUIRE(invoke->Invoke() == S_OK);
        REQUIRE_FALSE(calls.empty());
        // UIA 代理 Invoke 前可能先 SetFocus（kActionFocus 同走 dispatch）；
        // Activate 恒为最后一次。
        const DispatchCall& last = calls.back();
        CHECK(last.nodeId == "btn-ok");
        CHECK(last.action == accessibility::kActionActivate);
    } else {
        WARN("UIA client returned no Invoke pattern proxy; provider-side "
             "pattern coverage already asserted");
    }
    // 消息泵清空残留（UIA 内部投递）。
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

#endif  // defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)

}  // namespace
