#import <AppKit/AppKit.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "designer_app.h"
#include "nsaccessibility_provider.h"

using namespace lumen;
using namespace lumen::accessibility;

namespace {

id findAccessibilityElement(id element, NSString* role, NSString* title) {
    if (element == nil) return nil;
    const id elementRole =
        [element accessibilityAttributeValue:NSAccessibilityRoleAttribute];
    const id elementTitle =
        [element accessibilityAttributeValue:NSAccessibilityTitleAttribute];
    if ((role == nil || [elementRole isEqual:role]) &&
        (title == nil || [elementTitle isEqual:title])) {
        return element;
    }
    NSArray* children =
        [element accessibilityAttributeValue:NSAccessibilityChildrenAttribute];
    for (id child in children) {
        if (id found = findAccessibilityElement(child, role, title)) {
            return found;
        }
    }
    return nil;
}

id findWindowAccessibilityElement(NSWindow* window, NSString* role,
                                  NSString* title) {
    for (id root in [[window contentView] accessibilityChildren]) {
        if (id found = findAccessibilityElement(root, role, title)) {
            return found;
        }
    }
    return nil;
}

}  // namespace

// Provider design §7: native object identity, actions, coordinates and detach.
TEST_CASE("a11y_appkit_preserves_identity_and_detaches_retained_elements", "[a11y][appkit]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(30, 40, 320, 240)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        [window setReleasedWhenClosed:NO];
        PlatformAccessibilityHost host;
        host.nativeWindow = window;
        std::string target;
        std::uint32_t action = 0;
        host.dispatch = [&](const std::string& id, std::uint32_t kind,
                            const std::string&, float) {
            target = id;
            action = kind;
            return SemanticsActionStatus::Handled;
        };
        id retained = nil;
        {
            nsaccessibility::NsAccessibilityBridge bridge(host, nullptr);
            REQUIRE(bridge.available());
            SemanticsTree tree;
            tree.rootId = "root";
            SemanticsNode root;
            root.id = "root";
            root.children = {"button"};
            tree.nodes.emplace(root.id, root);
            SemanticsNode button;
            button.id = "button";
            button.label = "Apply";
            button.role = SemanticsRole::Button;
            button.actions = kActionActivate | kActionFocus;
            button.bounds = lumen::core::Rect::fromXYWH(10, 20, 80, 30);
            tree.nodes.emplace(button.id, button);
            bridge.updateTree(tree, {}, {});
            // The bridge attaches via the modern accessibilityChildren
            // property; the legacy NSAccessibilityChildrenAttribute readback
            // on NSView is no longer honored on current macOS.
            NSArray* roots = [[window contentView] accessibilityChildren];
            REQUIRE(roots.count == 1);
            id rootElement = roots[0];
            retained = [[[rootElement accessibilityAttributeValue:NSAccessibilityChildrenAttribute] objectAtIndex:0] retain];
            [retained accessibilityPerformAction:NSAccessibilityPressAction];
            CHECK(target == "button");
            CHECK(action == kActionActivate);
            [retained accessibilitySetValue:@YES forAttribute:NSAccessibilityFocusedAttribute];
            CHECK(action == kActionFocus);
            const auto point = [[retained accessibilityAttributeValue:NSAccessibilityPositionAttribute] pointValue];
            const auto expected = [window convertPointToScreen:NSMakePoint(10, 190)];
            CHECK(point.x == expected.x);
            CHECK(point.y == expected.y);
            tree.nodes.at("button").label = "Applied";
            bridge.updateTree(tree, {}, {});
            NSArray* children = [rootElement accessibilityAttributeValue:NSAccessibilityChildrenAttribute];
            CHECK(children[0] == retained);
            CHECK([[retained accessibilityAttributeValue:NSAccessibilityTitleAttribute] isEqualToString:@"Applied"]);
            tree.nodes.erase("button");
            tree.nodes.at("root").children.clear();
            bridge.updateTree(tree, {}, {});
            CHECK([retained accessibilityIsIgnored]);
            action = 0;
            [retained accessibilityPerformAction:NSAccessibilityPressAction];
            CHECK(action == 0);
        }
        CHECK([retained accessibilityAttributeValue:NSAccessibilityParentAttribute] == nil);
        [retained release];
        [window close];
        [window release];
    }
}

TEST_CASE("a11y_appkit_text_value_establishes_focus_before_commit",
          "[a11y][appkit]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(30, 40, 320, 240)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        [window setReleasedWhenClosed:NO];
        PlatformAccessibilityHost host;
        host.nativeWindow = window;
        std::vector<std::uint32_t> actions;
        std::vector<std::string> values;
        bool allowFocus = true;
        host.dispatch = [&](const std::string&, std::uint32_t action,
                            const std::string& value, float) {
            actions.push_back(action);
            values.push_back(value);
            if (action == kActionFocus && !allowFocus) {
                return SemanticsActionStatus::NotHandled;
            }
            return SemanticsActionStatus::Handled;
        };
        {
            nsaccessibility::NsAccessibilityBridge bridge(host, nullptr);
            REQUIRE(bridge.available());
            SemanticsTree tree;
            tree.rootId = "root";
            SemanticsNode root;
            root.id = tree.rootId;
            root.children = {"field"};
            tree.nodes.emplace(root.id, root);
            SemanticsNode field;
            field.id = "field";
            field.role = SemanticsRole::TextField;
            field.label = "Title";
            field.value = "Before";
            field.actions = kActionFocus | kActionSetValue;
            tree.nodes.emplace(field.id, field);
            bridge.updateTree(tree, {}, {});

            NSArray* roots = [[window contentView] accessibilityChildren];
            REQUIRE(roots.count == 1);
            id rootElement = roots[0];
            id fieldElement =
                [[rootElement accessibilityAttributeValue:
                    NSAccessibilityChildrenAttribute] objectAtIndex:0];

            [fieldElement accessibilitySetValue:@"After"
                                   forAttribute:NSAccessibilityValueAttribute];
            REQUIRE(actions.size() == 2);
            CHECK(actions[0] == kActionFocus);
            CHECK(actions[1] == kActionSetValue);
            CHECK(values[1] == "After");

            actions.clear();
            values.clear();
            allowFocus = false;
            [fieldElement accessibilitySetValue:@"Rejected"
                                   forAttribute:NSAccessibilityValueAttribute];
            REQUIRE(actions.size() == 1);
            CHECK(actions[0] == kActionFocus);
            CHECK(values[0].empty());
        }
        [window close];
        [window release];
    }
}

// Designer prerequisites §4.18: the native AX client must drive the same
// ThemeScope reference transaction as the desktop UI, including history and
// persistence.
TEST_CASE("a11y_appkit_drives_designer_theme_reference_transaction",
          "[a11y][appkit][designer]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(30, 40, 1280, 800)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered
                defer:NO];
        [window setReleasedWhenClosed:NO];
        [window makeKeyAndOrderFront:nil];

        const std::filesystem::path fixture =
            std::filesystem::temp_directory_path() /
            "lumen-designer-nsaccessibility.design";
        const std::filesystem::path saved =
            std::filesystem::temp_directory_path() /
            "lumen-designer-nsaccessibility-saved.design";
        std::error_code cleanup;
        std::filesystem::remove(fixture, cleanup);
        cleanup.clear();
        std::filesystem::remove(saved, cleanup);
        {
            std::ofstream output(fixture, std::ios::binary | std::ios::trunc);
            REQUIRE(output.good());
            output << R"DESIGN({
  "documentId": "nsaccessibility-designer",
  "format": "lumen.design",
  "pageName": "designer",
  "schemaVersion": 1,
  "root": {
    "id": "1",
    "type": "Column",
    "properties": {"key": {"kind": "string", "value": "root"}},
    "children": [
      {"id": "2", "type": "Text", "properties": {
        "key": {"kind": "string", "value": "title"},
        "text": {"kind": "string", "value": "Title"}
      }},
      {"id": "3", "type": "ThemeScope", "properties": {
        "key": {"kind": "string", "value": "theme-scope"}
      }, "references": {"theme": "light"}}
    ]
  }
})DESIGN";
        }

        designer_app::DesignerApp app;
        app.attach();
        REQUIRE(app.loadDesignFile(fixture.string()));
        app.shell().setView(core::Size{1280.0F, 800.0F});

        PlatformAccessibilityHost host;
        host.nativeWindow = window;
        host.applicationName = "Lumen Designer AX test";
        host.dispatch = [&app](const std::string& id, std::uint32_t action,
                               const std::string& value, float scrollDelta) {
            return app.shell().performAccessibilityAction(id, action, value,
                                                           scrollDelta);
        };
        host.activateWindow = [window] {
            [window makeKeyAndOrderFront:nil];
            return true;
        };

        std::string diagnostics;
        {
            nsaccessibility::NsAccessibilityBridge bridge(host, &diagnostics);
            REQUIRE(bridge.available());
            app.shell().setAccessibilityBridge(&bridge);
            bridge.noteWindowActive(true);
            (void)app.shell().renderFrame();

            id themeItem = findWindowAccessibilityElement(
                window, NSAccessibilityRowRole, @"ThemeScope  [theme-scope]");
            REQUIRE(themeItem != nil);
            [themeItem accessibilityPerformAction:NSAccessibilityPressAction];
            (void)app.shell().renderFrame();

            id field = findWindowAccessibilityElement(
                window, NSAccessibilityTextFieldRole, @"theme reference");
            REQUIRE(field != nil);
            CHECK([[field accessibilityAttributeValue:NSAccessibilityValueAttribute]
                       isEqualToString:@"light"]);
            CHECK([field accessibilityIsAttributeSettable:
                               NSAccessibilityValueAttribute]);
            [field accessibilitySetValue:@"dark"
                           forAttribute:NSAccessibilityValueAttribute];
            (void)app.shell().renderFrame();

            REQUIRE(app.workbench().document().has_value());
            CHECK(app.workbench().document()->root.children.back().references.at(
                      "theme") == "dark");
            CHECK(app.workbench().selection().primary == 3);
            CHECK(app.workbench().dirty());

            REQUIRE(app.undo());
            (void)app.shell().renderFrame();
            CHECK(app.workbench().document()->root.children.back().references.at(
                      "theme") == "light");
            REQUIRE(app.redo());
            (void)app.shell().renderFrame();
            CHECK(app.workbench().document()->root.children.back().references.at(
                      "theme") == "dark");

            REQUIRE(app.saveDesignFile(saved.string()));
        }

        designer_app::DesignerApp reopened;
        reopened.attach();
        REQUIRE(reopened.loadDesignFile(saved.string()));
        REQUIRE(reopened.workbench().document().has_value());
        CHECK(reopened.workbench().document()->documentId ==
              "nsaccessibility-designer");
        CHECK(reopened.workbench().document()->root.children.back().references.at(
                  "theme") == "dark");

        std::error_code ignored;
        std::filesystem::remove(fixture, ignored);
        std::filesystem::remove(saved, ignored);
        [window close];
        [window release];
    }
}
