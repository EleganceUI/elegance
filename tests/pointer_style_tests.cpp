// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <catch2/catch_test_macros.hpp>
#include <elg/elegance.h>

using namespace elg;

namespace {

// A 100 x 100 window: a background with the pointer style, and a 50 x 50 view in the middle
// with the text style while `shown`.
struct Window {
    detail::Scope scope;
    detail::Scope::Use use{scope};
    State<bool> shown{true};
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::optional<detail::EventDispatcher> events;

    Window() {
        const State<bool> visible = shown;
        root = detail::lower(ZStack(Rectangle().fill(Color::white).pointer_style(PointerStyle::pointer),
                                    If([=] { return visible(); },
                                       Rectangle().fill(Color::black).frame(50, 50).pointer_style(PointerStyle::text))),
                             {});
        layout();
        events.emplace(*root, focus);
    }

    void layout() {
        root->size_that_fits({100, 100});
        root->place({}, {100, 100});
    }
};

} // namespace

TEST_CASE("the innermost view under the pointer sets its style", "[pointer-style]") {
    Window window;
    window.events->pointer_moved({50, 50});
    CHECK(window.events->pointer_style() == PointerStyle::text);
    window.events->pointer_moved({5, 5});
    CHECK(window.events->pointer_style() == PointerStyle::pointer);
}

TEST_CASE("a view going away under a still pointer takes its style with it", "[pointer-style]") {
    // A text field closing on Enter used to leave the I-beam behind.
    Window window;
    window.events->pointer_moved({50, 50});
    REQUIRE(window.events->pointer_style() == PointerStyle::text);
    window.shown = false;
    detail::flush_effects();
    window.layout();
    window.events->revalidate();
    CHECK(window.events->pointer_style() == PointerStyle::pointer);
}

TEST_CASE("automatic leaves the style to the views around, and the arrow is the default", "[pointer-style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    auto root = detail::lower(Rectangle().fill(Color::white).pointer_style(PointerStyle::automatic), {});
    root->size_that_fits({100, 100});
    root->place({}, {100, 100});
    detail::FocusManager focus;
    detail::EventDispatcher events(*root, focus);
    events.pointer_moved({50, 50});
    CHECK(events.pointer_style() == PointerStyle::arrow);
}
