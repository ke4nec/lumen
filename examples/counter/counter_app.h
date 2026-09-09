#pragma once

// Counter application shared by the windowed example (main.cpp) and the
// headless integration tests. Owns the plan §5.2 frame pipeline:
// events -> state -> reconcile -> layout -> paint. All members are inline so
// both targets compile it directly.
//
// App-layer responsibilities per plan §6.1: state keys, event handlers and
// the frame loop live here, not inside the framework.

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

#include "lumen/core/element.h"
#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/dsl/dsl.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"

namespace lumen::examples {

class CounterApp {
  public:
    CounterApp() : element_(buildUi()) {
        state_.set("counter", "0");
        state_.set("name", "");
        handlers_["increment"] = [this] {
            state_.set("counter", std::to_string(counterValue() + 1));
        };
        syncSubscriptions(core::collectBindKeys(element_.widget()));
    }

    CounterApp(const CounterApp&) = delete;
    CounterApp& operator=(const CounterApp&) = delete;
    CounterApp(CounterApp&&) = delete;
    CounterApp& operator=(CounterApp&&) = delete;

    // The declarative UI (plan §6.1 example shape).
    [[nodiscard]] core::Widget buildUi() const {
        using namespace dsl;
        core::Widget page = container(
            column({core::withKey(text("Count: ", bind("counter")),
                                  "count-text"),
                    core::withKey(button("Increment", onClick("increment")),
                                  "increment-button"),
                    core::withKey(text_field(bind("name"), placeholder("Name")),
                                  "name-field")},
                   core::EdgeInsets::all(24.0F), 12.0F),
            core::Color::fromRGBA(24, 24, 27));
        page.key = "root";
        return page;
    }

    void setView(core::Size size) {
        if (size.width <= 0.0F || size.height <= 0.0F) {
            return;
        }
        view_ = size;
        dirty_ = true;
    }
    void setDeviceScale(float scale) { renderer_.setDeviceScale(scale); }

    // Reconcile + relayout when state or view changed (plan §5.2 steps 2-3).
    void rebuildIfDirty() {
        if (!dirty_) {
            return;
        }
        core::Widget next = buildUi();
        core::applyBinds(next, state_);
        element_.update(std::move(next));
        // Keep subscriptions in lockstep with the tree: newly bound keys get
        // observers, keys dropped by the rebuild are cleaned up (plan §9).
        syncSubscriptions(core::collectBindKeys(element_.widget()));
        root_ = layout::LayoutEngine::layout(element_.widget(),
                                              core::Constraints::tight(view_));
        dirty_ = false;
    }

    // Paint + post-frame bookkeeping; returns the frame hash (plan §9).
    std::uint64_t renderFrame() {
        rebuildIfDirty();
        renderer_.beginFrame(view_);
        render::PaintOptions options;
        options.focusedKey = focus_.focusedKey();
        options.focusedIdentity = focus_.focusedIdentity();
        options.pressedKey = controller_.pressedKey();
        options.pressedIdentity = controller_.pressedIdentity();
        options.caretCodePoints = controller_.caretCodePoints();
        render::paintScene(renderer_, root_, options);
        element_.clearDirtyTree();
        return render::frameHash(renderer_.pixels());
    }

    // Pointer in logical (root) coordinates; rebuilds first so hits land on
    // the current layout even when events and updates share one batch.
    void pointerDown(core::Offset position) {
        rebuildIfDirty();
        controller_.pointerDown(root_, position);
    }

    void pointerUp(core::Offset position) {
        rebuildIfDirty();
        controller_.pointerUp(root_, position);
    }

    void textInput(const std::string& text) { controller_.textInput(text); }

    void keyDown(core::Key key) { controller_.keyDown(key); }

    [[nodiscard]] int counterValue() const {
        return std::atoi(state_.get("counter").c_str());
    }
    [[nodiscard]] const core::StateStore& state() const { return state_; }
    [[nodiscard]] const core::RenderNode& root() const { return root_; }
    [[nodiscard]] const core::InteractionController& controller() const {
        return controller_;
    }
    [[nodiscard]] bool wantsTextInput() const {
        return controller_.wantsTextInput();
    }
    [[nodiscard]] const render::PixelBuffer& pixels() const {
        return renderer_.pixels();
    }

  private:
    // Diffs the subscribed bind keys against `keys`: subscribes new ones and
    // unsubscribes keys the latest tree no longer references.
    void syncSubscriptions(const std::set<std::string>& keys) {
        for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
            if (keys.count(it->first) == 0) {
                state_.unsubscribe(it->second);
                it = subscriptions_.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& key : keys) {
            if (subscriptions_.count(key) == 0) {
                subscriptions_.emplace(
                    key, state_.subscribe(key, [this] { dirty_ = true; }));
            }
        }
    }

    core::StateStore state_{};
    core::HandlerRegistry handlers_{};
    std::map<std::string, core::StateStore::ObserverId> subscriptions_{};
    core::FocusManager focus_{};
    core::InteractionController controller_{state_, handlers_, focus_};
    core::Element element_;
    core::RenderNode root_{};
    core::Size view_{800.0F, 600.0F};
    render::CpuRenderer renderer_{1.0F};
    bool dirty_{true};
};

}  // namespace lumen::examples
