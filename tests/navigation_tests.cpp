// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// NavigationStack's styles, transitions and back gestures; split view dividers.

#include "view/view_node.h"

#include <elg/navigation.h>
#include <elg/shapes.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace elg;

namespace {

// Records what it's given, and shows just the pages.
struct Seen {
    std::optional<NavigationStackConfiguration> stack;
};

class RecordingStackStyle final : public NavigationStackStyle {
public:
    explicit RecordingStackStyle(std::shared_ptr<Seen> seen) : seen_(std::move(seen)) {}
    View make_body(const NavigationStackConfiguration& stack) const override {
        seen_->stack = stack;
        return stack.pages;
    }

private:
    std::shared_ptr<Seen> seen_;
};

View Page() {
    return Rectangle().fill(Color::white);
}

detail::DisplayList paint(detail::Element& root) {
    root.size_that_fits({300.f, 200.f});
    root.place({}, {300.f, 200.f});
    detail::DisplayList list;
    root.paint(list);
    return list;
}

void push(const State<std::vector<int>>& path, int route) {
    path.update([&](std::vector<int>& p) { p.push_back(route); });
    detail::flush_effects();
}

} // namespace

TEST_CASE("a navigation stack's style gets the titles and goes back", "[navigation]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::vector<int>> path;
    const auto seen = std::make_shared<Seen>();
    const auto root = detail::lower(
        NavigationStack(path, Page(), [](int) { return Page(); },
                        {.root_title = "Notes", .title = [](const int& id) { return "Note " + std::to_string(id); }})
            .navigation_stack_style(RecordingStackStyle(seen)),
        {});
    REQUIRE(seen->stack);
    const NavigationStackConfiguration& stack = *seen->stack;
    CHECK(stack.titles() == std::vector<std::string>{"Notes"});
    CHECK_FALSE(stack.can_go_back());

    push(path, 7);
    CHECK(stack.titles() == std::vector<std::string>{"Notes", "Note 7"});
    CHECK(stack.can_go_back());
    stack.back();
    detail::flush_effects();
    CHECK(path().empty());
}

TEST_CASE("pages slide, fade, or change at once", "[navigation]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    for (const PageTransition transition : {PageTransition::slide, PageTransition::fade, PageTransition::none}) {
        const State<std::vector<int>> path;
        const auto root = detail::lower(
            NavigationStack(path, Page(), [](int) { return Page(); }, {.transition = transition}), {});
        CHECK_FALSE(paint(*root).animating);
        push(path, 1);
        CHECK(paint(*root).animating == (transition != PageTransition::none));
    }
}

TEST_CASE("Alt+Left goes back unless back gestures are off", "[navigation]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    for (const bool gestures : {true, false}) {
        const State<std::vector<int>> path(std::vector<int>{1});
        const auto root = detail::lower(
            NavigationStack(path, Page(), [](int) { return Page(); }, {.back_gestures = gestures}), {});
        EventHandler* const handler = root->event_handler();
        if (handler) {
            handler->key({.kind = KeyEvent::Kind::down, .key = Key::left, .modifiers = KeyModifiers::alt});
            detail::flush_effects();
        }
        CHECK(path().empty() == gestures);
    }
}

TEST_CASE("a split view's divider takes its thickness between the panes", "[navigation]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    for (const float thickness : {1.f, 0.f, 6.f}) {
        const auto root = detail::lower(
            HSplitView({.thickness = thickness}, Page().frame({.max_width = infinity}),
                       Page().frame({.max_width = infinity}).layout_priority(1)),
            {});
        root->size_that_fits({300.f, 100.f});
        root->place({}, {300.f, 100.f});
        std::vector<Rect> panes;
        root->for_each_child([&](detail::Element& child) { panes.push_back(child.frame()); });
        REQUIRE(panes.size() >= 2);
        CHECK(panes[1].x - panes[0].max_x() == thickness);
    }
}
