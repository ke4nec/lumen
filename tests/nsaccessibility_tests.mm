#import <AppKit/AppKit.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "nsaccessibility_provider.h"

using namespace lumen::accessibility;

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
