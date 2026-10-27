// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Pointer enter/leave fire at once; hover intent waits for the delay; controls' highlights don't.

#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/events.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace elg;

namespace {

// A 100 x 100 window with `view` in its top-left 50 x 50.
struct Window {
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::optional<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(ZStack({.alignment = Alignment::top_leading}, Rectangle().fill(Color::white),
                                    view.frame(50, 50)),
                             {});
        root->size_that_fits({100, 100});
        root->place({}, {100, 100});
        events.emplace(*root, focus);
    }
};

} // namespace

TEST_CASE("pointer enter and leave fire at once; hover intent waits", "[hover]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    int entered = 0;
    int left = 0;
    std::optional<bool> intent;
    Window window(Rectangle()
                      .fill(Color::black)
                      .on_pointer_enter([&] { ++entered; })
                      .on_pointer_leave([&] { ++left; })
                      .on_hover_change([&](bool inside) { intent = inside; }, HoverDelay{.enter = 60, .exit = 0}));

    window.events->pointer_moved({20, 20});
    CHECK(entered == 1);
    CHECK_FALSE(intent);              // still resting
    CHECK(window.events->next_deadline()); // the intent is due later

    window.events->pointer_moved({80, 80});
    CHECK(left == 1);
    window.events->tick();
    CHECK_FALSE(intent); // passed over: never counted as hovered
}

TEST_CASE("a button's highlight ignores the hover delay", "[hover]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::optional<bool> hovered;
    struct Recording final : ButtonStyle {
        std::optional<bool>* seen;
        explicit Recording(std::optional<bool>* s) : seen(s) {}
        View make_body(const ButtonConfiguration& button) const override {
            effect([seen = seen, hovered = button.is_hovered] { *seen = hovered(); });
            return button.label;
        }
    };
    Window window(Button(Rectangle().fill(Color::black), [] {})
                      .button_style(Recording(&hovered))
                      .hover_delay({.enter = 60, .exit = 60}));
    window.events->pointer_moved({20, 20});
    detail::flush_effects();
    CHECK(hovered == true);
    window.events->pointer_moved({80, 80});
    detail::flush_effects();
    CHECK(hovered == false);
}
