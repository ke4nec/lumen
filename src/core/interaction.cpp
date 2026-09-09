#include "lumen/core/interaction.h"

#include <algorithm>
#include <utility>

#include "lumen/core/utf8.h"

namespace lumen::core {

const RenderNode* hitTestChain(const RenderNode& node, Offset position,
                               std::vector<const RenderNode*>& chain) {
    if (!Rect{Offset{}, node.size}.contains(position)) {
        return nullptr;
    }
    // Reverse paint order: later children are on top, so they hit first.
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        if (const RenderNode* hit =
                hitTestChain(*it, position - it->offset, chain)) {
            chain.push_back(&node);
            return hit;
        }
    }
    chain.push_back(&node);
    return &node;
}

void FocusManager::setFocus(std::string key) {
    setFocus(std::move(key), {});
}

void FocusManager::setFocus(std::string key, std::string identity) {
    focusedKey_ = std::move(key);
    focusedIdentity_ = identity.empty() ? focusedKey_ : std::move(identity);
}

void FocusManager::clearFocus() {
    focusedKey_.clear();
    focusedIdentity_.clear();
}

const std::string& FocusManager::focusedKey() const { return focusedKey_; }

const std::string& FocusManager::focusedIdentity() const {
    return focusedIdentity_;
}

bool FocusManager::hasFocus(const std::string& key) const {
    return !focusedKey_.empty() && focusedKey_ == key;
}

InteractionController::InteractionController(StateStore& store,
                                             const HandlerRegistry& handlers,
                                             FocusManager& focus)
    : store_(store), handlers_(handlers), focus_(focus) {}

void InteractionController::pointerDown(const RenderNode& root,
                                        Offset position) {
    pressedKey_.clear();
    pressedIdentity_.clear();
    armedOnClick_.clear();
    armedKey_.clear();
    armedIdentity_.clear();
    std::vector<const RenderNode*> chain;
    const RenderNode* target = hitTestChain(root, position, chain);
    if (target == nullptr) {
        focus_.clearFocus();
        focusedBind_.clear();
        composition_.clear();
        caret_ = 0;
        return;
    }

    // Nearest button in the target chain gets the pressed state; its key
    // (required) stays stable across the rebuild between down and up.
    for (const RenderNode* node : chain) {
        if (node->type == WidgetType::Button) {
            pressedKey_ = node->key;
            pressedIdentity_ = node->identity;
            break;
        }
    }
    // Arm the nearest click target (first onClick walking target->root);
    // releasing over the same target fires it, anything else cancels.
    for (const RenderNode* node : chain) {
        if (!node->onClick.empty()) {
            armedOnClick_ = node->onClick;
            armedKey_ = node->key;
            armedIdentity_ = node->identity;
            break;
        }
    }

    // Nearest TextField grabs focus; clicking anywhere else releases it.
    const RenderNode* field = nullptr;
    for (const RenderNode* node : chain) {
        if (node->type == WidgetType::TextField) {
            field = node;
            break;
        }
    }
    if (field != nullptr && !field->bind.empty()) {
        focus_.setFocus(field->key.empty() ? field->bind : field->key,
                        field->identity);
        focusedBind_ = field->bind;
        composition_.clear();
        caret_ = utf8Length(store_.get(focusedBind_));
    } else {
        focus_.clearFocus();
        focusedBind_.clear();
        composition_.clear();
        caret_ = 0;
    }
}

void InteractionController::pointerUp(const RenderNode& root,
                                      Offset position) {
    const std::string armedOnClick = std::move(armedOnClick_);
    const std::string armedKey = std::move(armedKey_);
    const std::string armedIdentity = std::move(armedIdentity_);
    pressedKey_.clear();
    pressedIdentity_.clear();
    armedOnClick_.clear();
    armedKey_.clear();
    armedIdentity_.clear();
    if (armedOnClick.empty()) {
        return;
    }
    std::vector<const RenderNode*> chain;
    const RenderNode* target = hitTestChain(root, position, chain);
    if (target == nullptr) {
        return;
    }
    // The first onClick node on the release chain decides: firing only when
    // it is the same node armed at pointer down (plan §5.3: the event goes
    // to the target first, then bubbles to the nearest ancestor handler).
    for (const RenderNode* node : chain) {
        if (node->onClick.empty()) {
            continue;
        }
        if (node->onClick == armedOnClick && node->key == armedKey &&
            node->identity == armedIdentity) {
            const auto handler = handlers_.find(armedOnClick);
            if (handler != handlers_.end()) {
                handler->second();
            }
        }
        return;
    }
}

void InteractionController::textInput(const std::string& text) {
    if (focusedBind_.empty() || text.empty()) {
        return;
    }
    composition_.clear();
    const std::string value = store_.get(focusedBind_);
    store_.set(focusedBind_, utf8Insert(value, caret_, text));
    caret_ += utf8Length(text);
}

void InteractionController::setComposition(const std::string& text) {
    // Composition previews the IME string without touching the document;
    // committing still arrives as textInput(). Clearing focus drops it.
    // Ignore preedit while unfocused so composition_ never lingers without
    // a focused field (mirrors textInput()'s focusedBind_ guard).
    if (focusedBind_.empty()) {
        composition_.clear();
        return;
    }
    composition_ = text;
}

void InteractionController::keyDown(Key key) {
    if (focusedBind_.empty()) {
        return;
    }
    const std::string value = store_.get(focusedBind_);
    switch (key) {
        case Key::Backspace:
            store_.set(focusedBind_, utf8EraseBefore(value, caret_));
            caret_ = caret_ > 0 ? caret_ - 1 : 0;
            return;
        case Key::Delete:
            store_.set(focusedBind_, utf8EraseAfter(value, caret_));
            return;
        case Key::Left:
            caret_ = caret_ > 0 ? caret_ - 1 : 0;
            return;
        case Key::Right:
            caret_ = std::min(caret_ + 1, utf8Length(value));
            return;
        case Key::Home:
            caret_ = 0;
            return;
        case Key::End:
            caret_ = utf8Length(value);
            return;
        case Key::Enter:
        case Key::Escape:
            focus_.clearFocus();
            focusedBind_.clear();
            composition_.clear();
            caret_ = 0;
            return;
        case Key::None:
        case Key::Tab:
        case Key::Up:
        case Key::Down:
            return;
    }
}

}  // namespace lumen::core
