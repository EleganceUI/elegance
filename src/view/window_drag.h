// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_WINDOW_DRAG_H
#define ELG_VIEW_WINDOW_DRAG_H

#include "view/element.h"

#include <elg/events.h>

#include <functional>

namespace elg::detail {

// `.window_drag_area()`: marks where pressing moves the window. The window system does the
// dragging (via the hit test), so the handler itself takes no events.
class WindowDragHandler final : public EventHandler {
public:
    explicit WindowDragHandler(bool enabled) noexcept : enabled_(enabled) {}
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    // The whole area drags, gaps included, like a system title bar.
    [[nodiscard]] bool covers_frame() const override { return enabled_; }

private:
    bool enabled_;
};

// `.focusable(...)`: true makes the view a focus stop; false takes the view and everything inside
// it out of keyboard focus (Tab skips it, clicks don't focus it), like SwiftUI.
class FocusableHandler final : public EventHandler {
public:
    explicit FocusableHandler(bool focusable) noexcept : focusable_(focusable) {}
    [[nodiscard]] bool focusable() const override { return focusable_; }
    [[nodiscard]] bool blocks_focus() const noexcept { return !focusable_; }

private:
    bool focusable_;
};

// `.focused(binding)` around a view: focusable itself only when nothing inside is (so Tab and
// clicks reach the field it wraps, not an invisible wrapper first). focusable() walks what's
// inside; Tab learns the same from its own walk instead (collect_focus_order).
class FocusScopeHandler final : public EventHandler {
public:
    void set_scope(Element* scope) noexcept { scope_ = scope; }
    [[nodiscard]] bool focusable() const override {
        bool inner = false;
        std::function<void(Element&)> walk = [&](Element& element) {
            if (inner) {
                return;
            }
            if (EventHandler* handler = element.event_handler(); handler && handler->focusable()) {
                inner = true;
                return;
            }
            element.for_each_child(walk);
        };
        scope_->for_each_child(walk);
        return !inner;
    }

private:
    Element* scope_ = nullptr;
};

// Whether `handler` is a `.focused(binding)` scope's.
inline bool is_focus_scope(const EventHandler& handler) {
    return dynamic_cast<const FocusScopeHandler*>(&handler) != nullptr;
}

// Whether `element` is a `.focusable(false)` scope.
inline bool blocks_focus(Element& element) {
    const auto* handler = dynamic_cast<const FocusableHandler*>(element.event_handler());
    return handler && handler->blocks_focus();
}

} // namespace elg::detail

#endif // ELG_VIEW_WINDOW_DRAG_H
