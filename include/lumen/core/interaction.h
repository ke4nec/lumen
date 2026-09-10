#pragma once

#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"

namespace lumen::core {

// Platform-agnostic key codes. Platform layers map their native keys onto
// these; printable input arrives separately through text input events.
enum class Key : int {
    None = 0,
    Backspace,
    Tab,
    Enter,
    Escape,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    Delete,
};

// Hit testing walks the render tree in reverse paint order: the last child is
// on top and wins (plan §5.3). On success `chain` receives the nodes from the
// hit target up to the root — the bubbling order. `position` is in root
// (window-logical) coordinates; child offsets are resolved recursively.
[[nodiscard]] const RenderNode* hitTestChain(const RenderNode& node,
                                             Offset position,
                                             std::vector<const RenderNode*>& chain);

// Tracks which widget holds keyboard focus. Keys are widget keys; the
// interaction controller keeps them stable across rebuilds.
class FocusManager {
  public:
    void setFocus(std::string key);
    void setFocus(std::string key, std::string identity);
    void clearFocus();
    [[nodiscard]] const std::string& focusedKey() const;
    [[nodiscard]] const std::string& focusedIdentity() const;
    [[nodiscard]] bool hasFocus(const std::string& key) const;

  private:
    std::string focusedKey_{};
    std::string focusedIdentity_{};
};

// Drives Button and TextField behavior on top of the render tree (plan §5.3):
// pointer down/up with press tracking and click bubbling, focus selection,
// UTF-8-aware text editing through the StateStore. All mutations flow into
// the store; the app rebuilds on subscription callbacks.
//
// Basic gestures (plan 阶段6): moves fed through pointerMove distinguish a
// tap (down/up with less than kDragSlopPx displacement — still a click) from
// a drag (beyond the slop; the click is cancelled).
class InteractionController {
  public:
    InteractionController(StateStore& store, const HandlerRegistry& handlers,
                          FocusManager& focus);

    void pointerDown(const RenderNode& root, Offset position);
    void pointerMove(const RenderNode& root, Offset position);
    void pointerUp(const RenderNode& root, Offset position);
    void textInput(const std::string& text);
    void setComposition(const std::string& text);
    void keyDown(Key key);

    // Button key currently held down ("" when none) — pressed visuals.
    [[nodiscard]] const std::string& pressedKey() const { return pressedKey_; }
    [[nodiscard]] const std::string& pressedIdentity() const {
        return pressedIdentity_;
    }
    // True while the pressed pointer moved beyond the drag slop; a drag
    // release never fires a click.
    [[nodiscard]] bool isDragging() const { return dragging_; }
    // Drag displacement from the press anchor to the latest move.
    [[nodiscard]] Offset dragDelta() const {
        return dragCurrent_ - dragAnchor_;
    }
    // Caret position (code points) inside the focused TextField.
    [[nodiscard]] std::size_t caretCodePoints() const { return caret_; }
    // In-progress IME composition (TEXT_EDITING); never committed to the
    // StateStore. Kept so future preedit UI can render it (plan §10).
    [[nodiscard]] const std::string& composition() const {
        return composition_;
    }
    // True while a bound TextField is focused; drives platform text input.
    [[nodiscard]] bool wantsTextInput() const { return !focusedBind_.empty(); }
    [[nodiscard]] const FocusManager& focus() const { return focus_; }

  private:
    StateStore& store_;
    const HandlerRegistry& handlers_;
    FocusManager& focus_;
    std::string pressedKey_{};
    std::string pressedIdentity_{};
    // Click target armed at pointer down: nearest onClick node identity.
    std::string armedOnClick_{};
    std::string armedKey_{};
    std::string armedIdentity_{};
    std::string focusedBind_{};
    std::string composition_{};
    std::size_t caret_{0};
    // Gesture state: press anchor and current pointer while held.
    bool pressActive_{false};
    bool dragging_{false};
    Offset dragAnchor_{};
    Offset dragCurrent_{};
};

}  // namespace lumen::core
