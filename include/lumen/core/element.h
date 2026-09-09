#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lumen/core/widget.h"

namespace lumen::core {

// Runtime instance of a Widget. Owns parent/child links and the dirty flag
// used by invalidate. State subscriptions live in StateStore at the app
// layer (plan §7); Element tracks structure reuse (type + key) and
// dirtiness.
//
// UI-thread ownership: Element trees are confined to the UI thread. Callers
// must clear the dirty flag after build/layout/paint (see clearDirtyTree()).
class Element {
  public:
    explicit Element(Widget widget, Element* parent = nullptr);

    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;
    Element(Element&&) = delete;
    Element& operator=(Element&&) = delete;

    // Reconciles this element against a new Widget description.
    // Same type + key reuses the subtree; otherwise the subtree is rebuilt.
    // Always marks this element dirty; callers clear it after relayout.
    void update(Widget next);

    void markDirty();
    void clearDirty();
    // Clears the dirty flag for this whole subtree; apps call it once the
    // rebuilt tree has been laid out and painted.
    void clearDirtyTree();
    [[nodiscard]] bool isDirty() const;

    [[nodiscard]] const Widget& widget() const;
    [[nodiscard]] Element* parent() const;
    [[nodiscard]] const std::vector<std::unique_ptr<Element>>& children() const;

    [[nodiscard]] static bool canReuse(const Widget& current,
                                       const Widget& next);

  private:
    void inflateChildren();
    void reconcileChildren(const std::vector<Widget>& nextChildren);

    Widget widget_{};
    Element* parent_{nullptr};
    std::vector<std::unique_ptr<Element>> children_{};
    bool dirty_{true};
};

}  // namespace lumen::core
