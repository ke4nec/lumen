#include "nsaccessibility_provider.h"

#if defined(__APPLE__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_NSACCESSIBILITY)

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

@interface LumenAXElement : NSAccessibilityElement {
@public
    lumen::accessibility::nsaccessibility::NsAccessibilityBridge::Impl* bridge;
    LumenAXElement* parent;
    std::string identity;
    lumen::accessibility::SemanticsNode node;
    std::vector<LumenAXElement*> children;
}
- (id)accessibilityAttributeValue:(NSString*)attribute;
- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute;
- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute;
- (NSArray*)accessibilityActionNames;
- (void)accessibilityPerformAction:(NSString*)action;
@end

using lumen::accessibility::SemanticsRole;
using lumen::accessibility::kActionActivate;
using lumen::accessibility::kActionSetValue;
using lumen::accessibility::kActionFocus;
using lumen::accessibility::kSemanticsEnabled;
using lumen::accessibility::kSemanticsFocused;
using lumen::accessibility::kSemanticsChecked;
using lumen::accessibility::kSemanticsHidden;
using lumen::accessibility::nsaccessibility::NsAccessibilityBridge;

namespace {

NSString* roleFor(SemanticsRole role) {
    switch (role) {
        case SemanticsRole::Button:
            return NSAccessibilityButtonRole;
        case SemanticsRole::MenuItem:
            return NSAccessibilityMenuItemRole;
        case SemanticsRole::Checkbox:
        case SemanticsRole::Switch:
            return NSAccessibilityCheckBoxRole;
        case SemanticsRole::Radio:
            return NSAccessibilityRadioButtonRole;
        case SemanticsRole::TextField:
            return NSAccessibilityTextFieldRole;
        case SemanticsRole::Text:
            return NSAccessibilityStaticTextRole;
        case SemanticsRole::Image:
            return NSAccessibilityImageRole;
        case SemanticsRole::Slider:
        case SemanticsRole::Splitter:
            return NSAccessibilitySliderRole;
        case SemanticsRole::ProgressBar:
            return NSAccessibilityProgressIndicatorRole;
        case SemanticsRole::List:
            return NSAccessibilityListRole;
        case SemanticsRole::ListItem:
        case SemanticsRole::TreeItem:
            return NSAccessibilityRowRole;
        case SemanticsRole::Tree:
            return NSAccessibilityOutlineRole;
        case SemanticsRole::Menu:
            return NSAccessibilityMenuRole;
        case SemanticsRole::Dialog:
            // AppKit has no NSAccessibilityDialogRole; dialogs are windows
            // with the dialog subrole (see subroleFor below).
            return NSAccessibilityWindowRole;
        case SemanticsRole::Window:
            return NSAccessibilityWindowRole;
        case SemanticsRole::Toolbar:
            return NSAccessibilityToolbarRole;
        case SemanticsRole::StatusBar:
        case SemanticsRole::Group:
            return NSAccessibilityGroupRole;
        case SemanticsRole::SpinButton:
            return NSAccessibilityIncrementorRole;
    }
    return NSAccessibilityGroupRole;
}

NSString* subroleFor(SemanticsRole role) {
    switch (role) {
        case SemanticsRole::Dialog:
            return NSAccessibilityDialogSubrole;
        case SemanticsRole::Window:
            return NSAccessibilityStandardWindowSubrole;
        default:
            return nil;
    }
}

NSString* stringFor(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

}  // namespace

namespace lumen::accessibility::nsaccessibility {

class NsAccessibilityBridge::Impl {
  public:
    explicit Impl(const PlatformAccessibilityHost& input) : host(input) {}
    ~Impl();

    PlatformAccessibilityHost host;
    SemanticsTree tree;
    std::string focusedId;
    std::unordered_map<std::string, LumenAXElement*> elements;
    LumenAXElement* root{nullptr};
    NSWindow* window{nil};
    bool attached{false};

    void dispatchValue(const std::string& id, const std::string& value) {
        if (host.dispatch) {
            (void)host.dispatch(id, kActionSetValue, value, 0.0F);
        }
    }
    void activate(const std::string& id) {
        if (host.dispatch) {
            (void)host.dispatch(id, kActionActivate, {}, 0.0F);
        }
    }
    void detach();
};

void NsAccessibilityBridge::Impl::detach() {
    if (window != nil && root != nullptr) {
        [[window contentView] accessibilitySetOverrideValue:nil
                                               forAttribute:NSAccessibilityChildrenAttribute];
    }
    for (auto& [id, element] : elements) {
        (void)id;
        element->bridge = nullptr;
        element->parent = nil;
        element->children.clear();
        [element release];
    }
    elements.clear();
    root = nullptr;
    [window release];
    window = nil;
    attached = false;
}

NsAccessibilityBridge::Impl::~Impl() { detach(); }

}  // namespace lumen::accessibility::nsaccessibility

@implementation LumenAXElement

- (BOOL)isAccessibilityElement {
    return bridge != nullptr && (node.flags & kSemanticsHidden) == 0;
}

- (BOOL)accessibilityIsIgnored { return ![self isAccessibilityElement]; }

- (NSArray*)accessibilityAttributeNames {
    return @[NSAccessibilityRoleAttribute, NSAccessibilitySubroleAttribute,
             NSAccessibilityRoleDescriptionAttribute,
             NSAccessibilityTitleAttribute, NSAccessibilityDescriptionAttribute,
             NSAccessibilityValueAttribute, NSAccessibilityEnabledAttribute,
             NSAccessibilityFocusedAttribute, NSAccessibilityChildrenAttribute,
             NSAccessibilityParentAttribute, NSAccessibilityPositionAttribute,
             NSAccessibilitySizeAttribute, NSAccessibilityWindowAttribute];
}

- (id)accessibilityAttributeValue:(NSString*)attribute {
    if (bridge == nullptr) return nil;
    if ([attribute isEqualToString:NSAccessibilityRoleAttribute]) {
        return roleFor(node.role);
    }
    if ([attribute isEqualToString:NSAccessibilitySubroleAttribute]) {
        return subroleFor(node.role);
    }
    if ([attribute isEqualToString:NSAccessibilityRoleDescriptionAttribute]) {
        return NSAccessibilityRoleDescription(roleFor(node.role), subroleFor(node.role));
    }
    if ([attribute isEqualToString:NSAccessibilityTitleAttribute] ||
        [attribute isEqualToString:NSAccessibilityDescriptionAttribute]) {
        return stringFor(node.label);
    }
    if ([attribute isEqualToString:NSAccessibilityValueAttribute]) {
        if (node.role == SemanticsRole::Checkbox || node.role == SemanticsRole::Switch ||
            node.role == SemanticsRole::Radio) {
            return @((node.flags & kSemanticsChecked) != 0);
        }
        if (node.role == SemanticsRole::Slider ||
            node.role == SemanticsRole::ProgressBar ||
            node.role == SemanticsRole::Splitter) {
            return @(std::strtod(node.value.c_str(), nullptr));
        }
        return stringFor(node.value);
    }
    if ([attribute isEqualToString:NSAccessibilityEnabledAttribute]) {
        return @((node.flags & kSemanticsEnabled) != 0);
    }
    if ([attribute isEqualToString:NSAccessibilityFocusedAttribute]) {
        return @((node.flags & kSemanticsFocused) != 0);
    }
    if ([attribute isEqualToString:NSAccessibilityChildrenAttribute]) {
        NSMutableArray* result = [NSMutableArray arrayWithCapacity:children.size()];
        for (LumenAXElement* child : children) {
            [result addObject:child];
        }
        return result;
    }
    if ([attribute isEqualToString:NSAccessibilityParentAttribute]) {
        return parent != nil ? static_cast<id>(parent) : [bridge->window contentView];
    }
    if ([attribute isEqualToString:NSAccessibilityWindowAttribute]) {
        return bridge->window;
    }
    if ([attribute isEqualToString:NSAccessibilityPositionAttribute]) {
        NSRect rect = NSMakeRect(node.bounds.origin.x, node.bounds.origin.y,
                                 node.bounds.size.width, node.bounds.size.height);
        if (bridge != nullptr && bridge->window != nil) {
            NSView* view = [bridge->window contentView];
            if (![view isFlipped]) {
                rect.origin.y = NSHeight(view.bounds) - NSMaxY(rect);
            }
            rect = [view convertRect:rect toView:nil];
            rect = [bridge->window convertRectToScreen:rect];
        }
        return [NSValue valueWithPoint:rect.origin];
    }
    if ([attribute isEqualToString:NSAccessibilitySizeAttribute]) {
        return [NSValue valueWithSize:NSMakeSize(node.bounds.size.width,
                                                 node.bounds.size.height)];
    }
    return [super accessibilityAttributeValue:attribute];
}

- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute {
    if (bridge == nullptr || (node.flags & kSemanticsEnabled) == 0) return NO;
    return ([attribute isEqualToString:NSAccessibilityValueAttribute] &&
            (node.actions & kActionSetValue) != 0) ||
           ([attribute isEqualToString:NSAccessibilityFocusedAttribute] &&
            (node.actions & kActionFocus) != 0);
}

- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute {
    if (![self accessibilityIsAttributeSettable:attribute]) return;
    // Dispatch may synchronously replace the semantic tree. Copy identity first.
    const std::string target = identity;
    if ([attribute isEqualToString:NSAccessibilityFocusedAttribute]) {
        if ([value boolValue] && bridge->host.dispatch) {
            (void)bridge->host.dispatch(target, kActionFocus, {}, 0.0F);
        }
        return;
    }
    if (![attribute isEqualToString:NSAccessibilityValueAttribute] ||
        bridge == nullptr || (node.actions & kActionSetValue) == 0) {
        return;
    }
    std::string text;
    if ([value isKindOfClass:[NSNumber class]]) {
        text = std::to_string([value doubleValue]);
    } else if ([value isKindOfClass:[NSString class]]) {
        const char* utf8 = [value UTF8String];
        text = utf8 != nullptr ? utf8 : "";
    }
    bridge->dispatchValue(target, text);
}

- (NSArray*)accessibilityActionNames {
    return bridge != nullptr && (node.flags & kSemanticsEnabled) != 0 &&
           (node.actions & kActionActivate) != 0 ? @[NSAccessibilityPressAction] : @[];
}

- (void)accessibilityPerformAction:(NSString*)action {
    if (bridge != nullptr &&
        [action isEqualToString:NSAccessibilityPressAction] &&
        (node.actions & kActionActivate) != 0 && (node.flags & kSemanticsEnabled) != 0) {
        const std::string target = identity;
        bridge->activate(target);
    }
}

@end

namespace lumen::accessibility::nsaccessibility {

NsAccessibilityBridge::NsAccessibilityBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics)
    : impl_(std::make_unique<Impl>(host)) {
    impl_->window = static_cast<NSWindow*>(host.nativeWindow);
    if (impl_->window == nil) {
        if (diagnostics != nullptr) {
            *diagnostics = "NSAccessibility requires an NSWindow";
        }
        return;
    }
    NSView* content = [impl_->window contentView];
    if (content == nil) {
        if (diagnostics != nullptr) {
            *diagnostics = "NSAccessibility window has no content view";
        }
        impl_->window = nil;
        return;
    }
    [content setAccessibilityElement:YES];
    [impl_->window retain];
    impl_->attached = true;
}

NsAccessibilityBridge::~NsAccessibilityBridge() = default;

bool NsAccessibilityBridge::available() const { return impl_->attached; }

void NsAccessibilityBridge::updateTree(const SemanticsTree& tree,
                                       const SemanticsDiff& diff,
                                       const std::string& focusedId) {
    impl_->tree = tree;
    impl_->focusedId = focusedId;
    if (!impl_->attached) {
        return;
    }
    for (auto it = impl_->elements.begin(); it != impl_->elements.end();) {
        auto* element = it->second;
        element->parent = nil;
        element->children.clear();
        if (tree.nodes.find(it->first) == tree.nodes.end()) {
            element->bridge = nullptr;
            [element release];
            it = impl_->elements.erase(it);
        } else {
            ++it;
        }
    }
    impl_->root = nullptr;
    for (const auto& [id, node] : tree.nodes) {
        auto [it, inserted] = impl_->elements.try_emplace(id, nil);
        if (inserted) it->second = [[LumenAXElement alloc] init];
        auto* element = it->second;
        element->bridge = impl_.get();
        element->identity = id;
        element->node = node;
    }
    for (const auto& [id, node] : tree.nodes) {
        auto* element = impl_->elements.at(id);
        for (const auto& childId : node.children) {
            const auto child = impl_->elements.find(childId);
            if (child != impl_->elements.end()) {
                element->children.push_back(child->second);
                child->second->parent = element;
            }
        }
    }
    const auto root = impl_->elements.find(tree.rootId);
    if (root != impl_->elements.end()) {
        impl_->root = root->second;
    }
    [[impl_->window contentView]
        accessibilitySetOverrideValue:impl_->root != nullptr ? @[impl_->root] : @[]
                         forAttribute:NSAccessibilityChildrenAttribute];
    if (!diff.added.empty() || !diff.removed.empty()) {
        NSAccessibilityPostNotification(impl_->window,
                                        NSAccessibilityLayoutChangedNotification);
    }
    for (const auto& id : diff.changed) {
        const auto it = impl_->elements.find(id);
        if (it != impl_->elements.end()) {
            NSAccessibilityPostNotification(it->second,
                                            NSAccessibilityValueChangedNotification);
        }
    }
}

void NsAccessibilityBridge::setFocusedNode(const std::string& id) {
    impl_->focusedId = id;
    for (auto& [identity, element] : impl_->elements) {
        element->node.flags = identity == id
                                  ? element->node.flags | kSemanticsFocused
                                  : element->node.flags & ~kSemanticsFocused;
    }
    const auto it = impl_->elements.find(id);
    if (it != impl_->elements.end()) {
        NSAccessibilityPostNotification(
            it->second, NSAccessibilityFocusedUIElementChangedNotification);
    }
}

std::size_t NsAccessibilityBridge::nodeCountForTesting() const {
    return impl_->elements.size();
}

std::unique_ptr<AccessibilityBridge> createNsAccessibilityBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics) {
    auto bridge = std::make_unique<NsAccessibilityBridge>(host, diagnostics);
    if (!bridge->available()) {
        return nullptr;
    }
    return bridge;
}

}  // namespace lumen::accessibility::nsaccessibility

#endif  // defined(__APPLE__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_NSACCESSIBILITY)
