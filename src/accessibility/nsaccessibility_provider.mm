#include "nsaccessibility_provider.h"

#if defined(__APPLE__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_NSACCESSIBILITY)

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen::accessibility::nsaccessibility {
class LumenAXElement;
}

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
using lumen::accessibility::kSemanticsEnabled;
using lumen::accessibility::kSemanticsFocused;
using lumen::accessibility::nsaccessibility::NsAccessibilityBridge;

namespace {

NSString* roleFor(SemanticsRole role) {
    switch (role) {
        case SemanticsRole::Button:
        case SemanticsRole::MenuItem:
            return NSAccessibilityButtonRole;
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
            return NSAccessibilityDialogRole;
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
        [element release];
    }
    elements.clear();
    root = nullptr;
    window = nil;
    attached = false;
}

NsAccessibilityBridge::Impl::~Impl() { detach(); }

}  // namespace lumen::accessibility::nsaccessibility

@implementation LumenAXElement

- (id)accessibilityAttributeValue:(NSString*)attribute {
    if ([attribute isEqualToString:NSAccessibilityRoleAttribute]) {
        return roleFor(node.role);
    }
    if ([attribute isEqualToString:NSAccessibilityRoleDescriptionAttribute]) {
        return NSAccessibilityRoleDescription(roleFor(node.role), nil);
    }
    if ([attribute isEqualToString:NSAccessibilityTitleAttribute] ||
        [attribute isEqualToString:NSAccessibilityDescriptionAttribute]) {
        return stringFor(node.label);
    }
    if ([attribute isEqualToString:NSAccessibilityValueAttribute]) {
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
        return parent;
    }
    if ([attribute isEqualToString:NSAccessibilityPositionAttribute]) {
        NSRect rect = NSMakeRect(node.bounds.origin.x, node.bounds.origin.y,
                                 node.bounds.size.width, node.bounds.size.height);
        if (bridge != nullptr && bridge->window != nil) {
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
    return [attribute isEqualToString:NSAccessibilityValueAttribute] &&
           (node.actions & kActionSetValue) != 0;
}

- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute {
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
    bridge->dispatchValue(identity, text);
}

- (NSArray*)accessibilityActionNames {
    return (node.actions & kActionActivate) != 0 ? @[NSAccessibilityPressAction] : @[];
}

- (void)accessibilityPerformAction:(NSString*)action {
    if (bridge != nullptr &&
        [action isEqualToString:NSAccessibilityPressAction] &&
        (node.actions & kActionActivate) != 0) {
        bridge->activate(identity);
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
    for (auto& [id, element] : impl_->elements) {
        (void)id;
        [element release];
    }
    impl_->elements.clear();
    impl_->root = nullptr;
    for (const auto& [id, node] : tree.nodes) {
        auto* element = [[LumenAXElement alloc] init];
        element->bridge = impl_.get();
        element->identity = id;
        element->node = node;
        impl_->elements.emplace(id, element);
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
        [[impl_->window contentView]
            accessibilitySetOverrideValue:@[impl_->root]
                             forAttribute:NSAccessibilityChildrenAttribute];
    }
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
