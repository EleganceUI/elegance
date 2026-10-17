// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_FOCUS_H
#define ELG_VIEW_FOCUS_H

#include "view/element.h"
#include "view/window_services.h"

#include <functional>
#include <vector>

namespace elg::detail {

// One window's keyboard focus (design.md §10): which element keys go to first, and whether the
// focus ring shows. Like the web's :focus-visible, the ring shows for keyboard focus (Tab) and
// programmatic focus, not for clicks. Outlives the window's element tree.
class FocusManager {
public:
    // `services` turns text input on while a view that accepts text has focus.
    explicit FocusManager(WindowServices* services = nullptr) noexcept : services_(services) {}

    Element* focused() const noexcept { return focused_; }
    bool ring_visible() const noexcept { return visible_; }
    // Whether focus last arrived by Tab (not a click or code): text fields select all then.
    bool navigated() const noexcept { return navigated_; }

    // Moves focus to `element` (nullptr: none) and tells the handlers that lost and gained it.
    // `navigated`: by Tab. Returns true when the window needs a new frame.
    bool focus(Element* element, bool visible, bool navigated = false);
    // Whether the focused element is `scope` or inside it.
    bool contains_focus(const Element* scope) const noexcept;
    // The first element in `scope` (descendants in tree order, then `scope` itself) that can take
    // focus, if any.
    static Element* first_focusable(Element& scope);

    // Calls `changed(true/false)` when focus moves into or out of `scope` (`.focused(binding)`).
    void watch(Element* scope, std::function<void(bool)> changed);

    // Called when an element that joined focus is destroyed: drops focus (and any watch) without
    // touching it.
    void forget(const Element* element) noexcept;

private:
    struct Watch {
        Element* scope;
        std::function<void(bool)> changed;
        bool within;
    };

    void notify_watches();

    void set_text_input(bool on) noexcept {
        if (services_ && on != text_input_) {
            text_input_ = on;
            on ? services_->start_text_input() : services_->stop_text_input();
        }
    }

    WindowServices* services_;
    std::vector<Watch> watches_;
    bool text_input_ = false;
    Element* focused_ = nullptr;
    bool visible_ = false;
    bool navigated_ = false;
};

} // namespace elg::detail

#endif // ELG_VIEW_FOCUS_H
