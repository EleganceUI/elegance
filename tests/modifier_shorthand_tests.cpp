// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Shorthands that are their compositions (clipped, border, the text tooltip), and Text's reactive
// font arguments.

#include "text/text_system.h"
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/text.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace elg;

namespace {

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// A 100 x 100 window with `view` in its top-left 50 x 50.
struct Window {
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::optional<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(ZStack({.alignment = Alignment::top_leading}, Rectangle().fill(Color::white),
                                    view.frame(50, 50)),
                             {.text = &text_system()});
        root->size_that_fits({100, 100});
        root->place({}, {100, 100});
        events.emplace(*root, focus);
    }
};

// Content that spills past its 50 x 50 frame, entered or not at (80, 80).
bool entered_outside(bool clipped) {
    bool entered = false;
    const View spill = Rectangle()
                           .fill(Color::black)
                           .frame(100, 100)
                           .on_pointer_enter([&] { entered = true; })
                           .frame(50, 50, Alignment::top_leading);
    Window window(clipped ? spill.clipped() : spill);
    window.events->pointer_moved({80, 80});
    return entered;
}

} // namespace

TEST_CASE("clipped is clip_shape(Rectangle()), for drawing and hit testing", "[shorthand]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    CHECK(entered_outside(false));
    CHECK_FALSE(entered_outside(true));
}

TEST_CASE("borders, tooltips and menu buttons build from their compositions", "[shorthand]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> on(false);
    CHECK_NOTHROW(Window(Rectangle().border(ThemeColor::outline)));
    CHECK_NOTHROW(Window(Rectangle().border([=] { return on() ? Color::black : Color::white; }, 2, Circle())));
    CHECK_NOTHROW(Window(Rectangle().tooltip("Settings")));
    CHECK_NOTHROW(Window(Rectangle().tooltip([=] { return on() ? std::string("On") : std::string("Off"); },
                                             {.delay = 0.2})));
    CHECK_NOTHROW(Window(MenuButton("File", [] { return Menu(MenuItem("Open", [] {})); },
                                    {.placement = {.edge = FloatingEdge::top, .spacing = 4}})));
}

TEST_CASE("Text's font and weight can be reactive", "[shorthand][text]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<float> size(12);
    const State<Weight> weight(Weight::regular);
    const auto root = detail::lower(Text("Elegance")
                                        .font([=] { return Font::system(size()); })
                                        .font_weight([=] { return weight(); }),
                                    {.text = &text_system()});
    const Size small = root->size_that_fits({});
    size = 24;
    detail::flush_effects();
    const Size large = root->size_that_fits({});
    CHECK(large.width > small.width * 1.5f);
    CHECK(large.height > small.height);

    weight = Weight::black;
    detail::flush_effects();
    CHECK(root->size_that_fits({}).width > large.width); // heavier glyphs are wider
}
