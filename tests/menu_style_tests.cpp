// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// MenuStyle::make_item: menu rows get their parts and state from the row, and their look from the
// style; the row keeps its behavior.

#include "text/text_system.h"
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

using namespace elg;

namespace {

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// Records what each row gets, and draws it as a 100 x 20 block.
struct RecordingMenuStyle final : MenuStyle {
    struct Row {
        bool has_leading;
        bool has_trailing;
        Signal<bool> highlighted;
        Signal<bool> enabled;
        Signal<bool> destructive;
    };
    explicit RecordingMenuStyle(std::vector<Row>* seen) : rows(seen) {}
    std::vector<Row>* rows;

    View make_body(const MenuConfiguration& menu) const override { return menu.items; }
    View make_item(const MenuItemConfiguration& item) const override {
        rows->push_back({!item.leading.is_empty(), !item.trailing.is_empty(), item.is_highlighted, item.is_enabled,
                         item.is_destructive});
        return Rectangle().fill(Color::black).frame(100, 20);
    }
};

struct Window {
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::optional<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(ZStack({.alignment = Alignment::top_leading}, Rectangle().fill(Color::white), view),
                             {.text = &text_system(), .focus = &focus});
        root->size_that_fits({200, 200});
        root->place({}, {200, 200});
        events.emplace(*root, focus);
    }
};

} // namespace

TEST_CASE("menu rows take their look from the menu style", "[menu][style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<RecordingMenuStyle::Row> rows;
    int opened = 0;
    Window window(VStack({.spacing = 0},
                         MenuItem("Open", [&] { ++opened; }, {.shortcut = "Ctrl+O"}),
                         MenuItem("Delete", [] {}, {.destructive = true, .checked = constant(true)}),
                         MenuItem("Disabled", [] {}).disabled(),
                         Submenu("More", [] { return Menu(MenuItem("Inner", [] {})); }))
                      .menu_style(RecordingMenuStyle(&rows)));
    REQUIRE(rows.size() == 4);

    CHECK_FALSE(rows[0].has_leading); // no check mark or icon
    CHECK(rows[0].has_trailing);      // the shortcut
    CHECK_FALSE(rows[0].destructive());
    CHECK(rows[1].has_leading); // the check mark
    CHECK_FALSE(rows[1].has_trailing);
    CHECK(rows[1].destructive());
    CHECK(rows[0].enabled());
    CHECK_FALSE(rows[2].enabled());
    CHECK(rows[3].has_trailing); // the submenu's chevron

    // Hovering a row highlights it; the row still takes the click.
    CHECK_FALSE(rows[0].highlighted());
    window.events->pointer_moved({50, 10});
    detail::flush_effects();
    CHECK(rows[0].highlighted());
    CHECK_FALSE(rows[1].highlighted());
    window.events->pointer_button(PointerEvent::Kind::down, {50, 10}, MouseButton::left, 1, KeyModifiers::none);
    window.events->pointer_button(PointerEvent::Kind::up, {50, 10}, MouseButton::left, 1, KeyModifiers::none);
    detail::flush_effects();
    CHECK(opened == 1);
}

TEST_CASE("the default menu row builds", "[menu][style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    CHECK_NOTHROW(Window(Menu(MenuItem("Copy", [] {}, {.shortcut = "Ctrl+C", .image = ImageSource{.system_name = "copy"}}),
                              Separator(), Submenu("Share", [] { return Menu(MenuItem("Mail", [] {})); }))));
}
