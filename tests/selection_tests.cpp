// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// View::selection: rows of a stack over a ForEach, selected with clicks, keys and assistive
// technology, and drawn by the RowStyle.

#include "text/text_system.h"
#include "view/animated.h"
#include "view/accessibility.h"
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <set>
#include <vector>

using namespace elg;

namespace {

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// Each row is a 100 x 20 block; records what each row's style was given, by row order.
struct RecordingRowStyle final : RowStyle {
    explicit RecordingRowStyle(std::vector<RowConfiguration>* seen) : rows(seen) {}
    std::vector<RowConfiguration>* rows;
    View make_body(const RowConfiguration& row) const override {
        rows->push_back(row);
        return row.content;
    }
};

View Block() {
    return Rectangle().fill(Color::black).frame(100, 20);
}

struct Window {
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::optional<detail::EventDispatcher> events;
    Size size;

    explicit Window(const View& view, Size size = {200, 200}) : size(size) {
        root = detail::lower(ZStack({.alignment = Alignment::top_leading}, Rectangle().fill(Color::white), view),
                             {.text = &text_system(), .focus = &focus});
        layout();
        events.emplace(*root, focus);
    }
    void layout() {
        detail::flush_effects();
        root->size_that_fits({size.width, size.height});
        root->place({}, {size.width, size.height});
        if (events) {
            events->revalidate(); // a lazy stack may have destroyed rows, as the app does after layout
        }
    }
    void click(Point at, KeyModifiers modifiers = KeyModifiers::none, int count = 1) {
        events->pointer_button(PointerEvent::Kind::down, at, MouseButton::left, count, modifiers);
        events->pointer_button(PointerEvent::Kind::up, at, MouseButton::left, count, modifiers);
        detail::flush_effects();
    }
    void press(Key key, KeyModifiers modifiers = KeyModifiers::none) {
        events->key({.kind = KeyEvent::Kind::down, .key = key, .modifiers = modifiers});
        events->key({.kind = KeyEvent::Kind::up, .key = key, .modifiers = modifiers});
        detail::flush_effects();
    }
};

// The middle of row `index` in a VStack of 20-point rows at the top of the window.
Point row(int index) {
    return {50, 10.f + 20.f * static_cast<float>(index)};
}

const std::vector<int> kItems{10, 20, 30, 40};

} // namespace

TEST_CASE("a click selects a row; the style sees which", "[selection]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::optional<int>> selected;
    std::vector<RowConfiguration> rows;
    Window window(VStack({.spacing = 0}, ForEach(kItems, [](int) { return Block(); }))
                      .selection(selected.binding())
                      .row_style(RecordingRowStyle(&rows)));
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].axis == Axis::vertical);
    CHECK_FALSE(rows[1].is_selected());

    window.click(row(1));
    CHECK(selected() == 20);
    CHECK(rows[1].is_selected());
    CHECK_FALSE(rows[0].is_selected());

    // A single selection: Ctrl+click (Cmd) clears it; plain clicks move it.
    window.click(row(1), primary_modifier());
    CHECK(selected() == std::nullopt);
    window.click(row(3));
    CHECK(selected() == 40);
    CHECK(rows[3].is_selected());
    CHECK_FALSE(rows[1].is_selected());

    // Setting the binding selects too; hovering shows.
    selected = 10;
    detail::flush_effects();
    CHECK(rows[0].is_selected());
    window.events->pointer_moved(row(2));
    detail::flush_effects();
    CHECK(rows[2].is_hovered());
    CHECK_FALSE(rows[1].is_hovered());
}

TEST_CASE("controls in a row keep their clicks; other children aren't rows", "[selection]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::optional<int>> selected;
    int pressed = 0;
    std::vector<RowConfiguration> rows;
    Window window(VStack({.spacing = 0},
                         Block(), // a header
                         ForEach(kItems, [&](int) {
                             return HStack({.spacing = 0}, Button(Block(), [&] { ++pressed; }).button_style(PlainButtonStyle{}),
                                           Spacer());
                         }))
                      .frame({.width = 200, .alignment = Alignment::leading})
                      .selection(selected.binding())
                      .row_style(RecordingRowStyle(&rows)));
    CHECK(rows.size() == 4); // not the header
    window.click(row(0));    // the header
    CHECK(selected() == std::nullopt);
    window.click(row(1)); // the first row's button
    CHECK(pressed == 1);
    CHECK(selected() == std::nullopt);
    window.click({150, row(1).y}); // the row beside it, in the spacer's gap
    CHECK(selected() == 10);
}

TEST_CASE("multiple selection: Ctrl/Cmd toggles, Shift selects ranges, select all", "[selection]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::set<int>> selected;
    Window window(VStack({.spacing = 0}, ForEach(kItems, [](int) { return Block(); }))
                      .selection(selected.binding())
                      .row_style(PlainRowStyle{}));

    window.click(row(1));
    CHECK(selected() == std::set<int>{20});
    window.click(row(3), KeyModifiers::shift);
    CHECK(selected() == std::set<int>{20, 30, 40});
    window.click(row(0), KeyModifiers::shift); // from the same anchor
    CHECK(selected() == std::set<int>{10, 20});
    window.click(row(3), primary_modifier());
    CHECK(selected() == std::set<int>{10, 20, 40});
    window.click(row(0), primary_modifier());
    CHECK(selected() == std::set<int>{20, 40});
    window.press(Key::a, primary_modifier());
    CHECK(selected() == std::set<int>{10, 20, 30, 40});
    window.click(row(2));
    CHECK(selected() == std::set<int>{30});
}

TEST_CASE("the keyboard moves the selection and activates", "[selection]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::optional<int>> selected;
    std::vector<int> opened;
    std::vector<RowConfiguration> rows;
    Window window(VStack({.spacing = 0}, ForEach(kItems, [](int) { return Block(); }))
                      .selection(selected.binding(), {.on_activate = [&](int id) { opened.push_back(id); }})
                      .row_style(RecordingRowStyle(&rows)));

    // One Tab stop; the first Down selects the first row.
    window.press(Key::tab);
    window.press(Key::down);
    CHECK(selected() == 10);
    CHECK(rows[0].is_focused());
    window.press(Key::down);
    window.press(Key::down);
    CHECK(selected() == 30);
    CHECK(rows[2].is_focused());
    CHECK_FALSE(rows[0].is_focused());
    window.press(Key::up);
    CHECK(selected() == 20);
    window.press(Key::end);
    CHECK(selected() == 40);
    window.press(Key::down); // stays at the end
    CHECK(selected() == 40);
    window.press(Key::home);
    CHECK(selected() == 10);
    window.press(Key::enter);
    CHECK(opened == std::vector<int>{10});

    // A double click activates the row it's on.
    window.click(row(2));
    window.click(row(2), KeyModifiers::none, 2);
    CHECK(selected() == 30);
    CHECK(opened == std::vector<int>{10, 30});
}

TEST_CASE("Shift+arrows extend a multiple selection; an HStack uses Left/Right", "[selection]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::set<int>> selected;
    Window window(HStack({.spacing = 0}, ForEach(kItems, [](int) { return Rectangle().frame(20, 20); }))
                      .selection(selected.binding())
                      .row_style(PlainRowStyle{}));
    window.click({30, 10}); // the second
    window.press(Key::right, KeyModifiers::shift);
    window.press(Key::right, KeyModifiers::shift);
    CHECK(selected() == std::set<int>{20, 30, 40});
    window.press(Key::left, KeyModifiers::shift);
    CHECK(selected() == std::set<int>{20, 30});
    window.press(Key::left);
    CHECK(selected() == std::set<int>{20});
    window.press(Key::down); // not this stack's axis
    CHECK(selected() == std::set<int>{20});
}

TEST_CASE("a lazy stack's rows select by key, built or not", "[selection][lazy]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<int> items;
    for (int i = 0; i < 1000; ++i) {
        items.push_back(i);
    }
    const State<std::optional<int>> selected;
    Window window(ScrollView(LazyVStack({.spacing = 0}, ForEach(items, [](int) { return Block(); }))
                                 .selection(selected.binding())
                                 .row_style(PlainRowStyle{})),
                  {200, 100});
    window.click(row(2));
    CHECK(selected() == 2);
    window.press(Key::end); // far out of view, not built
    CHECK(selected() == 999);
    // It scrolls to where the row would be, then to the row once built.
    for (int i = 1; i <= 40; ++i) {
        detail::set_frame_time(static_cast<double>(i) * 0.05);
        window.layout();
    }
    window.click({50, 90}); // the bottom of the view
    CHECK(selected() == 999);
    window.press(Key::up);
    CHECK(selected() == 998);
    detail::set_frame_time(0);
}

TEST_CASE("screen readers see a list box of options", "[selection][accessibility]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::set<int>> selected(std::set<int>{20});
    Window window(VStack({.spacing = 0}, ForEach(kItems, [](int) { return Block(); })).selection(selected.binding()));
    const detail::AccessTree tree = detail::build_accessibility_tree(*window.root, "Test", window.size, nullptr);
    std::vector<const detail::AccessNode*> options;
    const detail::AccessNode* list = nullptr;
    for (const detail::AccessNode& node : tree.nodes) {
        if (node.info.role == detail::AccessRole::list_box) {
            list = &node;
        } else if (node.info.role == detail::AccessRole::list_box_option) {
            options.push_back(&node);
        }
    }
    REQUIRE(list);
    CHECK(list->info.multiselectable);
    CHECK(list->info.can_focus);
    REQUIRE(options.size() == 4);
    CHECK_FALSE(options[0]->info.selected);
    CHECK(options[1]->info.selected);
    CHECK(options[2]->info.can_click);

    // Its click selects.
    const auto target = tree.targets.find(options[2]->id);
    REQUIRE(target != tree.targets.end());
    CHECK(target->second.element->accessibility_action({}));
    detail::flush_effects();
    CHECK(selected() == std::set<int>{30});
}

TEST_CASE("rows can read whether they're selected; the default style builds", "[selection][style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::optional<std::string>> selected;
    std::map<std::string, std::function<bool()>> readers;
    struct Row {
        std::string name;
        std::map<std::string, std::function<bool()>>* readers;
        View body() const {
            const auto selected = Environment<RowSelectedKey>();
            (*readers)[name] = [selected] { return selected(); };
            return Text(name);
        }
    };
    const std::vector<std::string> names{"a", "b"};
    Window window(VStack(ForEach(names, [&](const std::string& name) { return View(Row{name, &readers}); }))
                      .selection(selected.binding()));
    REQUIRE(readers.size() == 2);
    CHECK_FALSE(readers["b"]());
    selected = std::string("b");
    detail::flush_effects();
    CHECK(readers["b"]());
    CHECK_FALSE(readers["a"]());

    CHECK_NOTHROW(Window(VStack(ForEach(names, [](const std::string& name) { return Text(name); }))
                             .selection(selected.binding())
                             .row_style(HighlightRowStyle{{.corner_radius = 0}})
                             .disabled()));
}
