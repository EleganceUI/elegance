// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Text::selectable(): selecting with the pointer, and the highlight (ELGSPEC-0030).

#include "display/display_list.h"
#include "text/text_system.h"
#include "view/element.h"
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"
#include "view/window_services.h"

#include <elg/environment.h>
#include <elg/events.h>
#include <elg/reactive.h>
#include <elg/text.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using namespace elg;

namespace {

constexpr Color kRed = Color::rgb(255, 0, 0);

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// The clipboard, and nothing else a window does.
class ClipboardServices final : public detail::WindowServices {
public:
    std::string clipboard;

    void start_text_input(bool) override {}
    void stop_text_input() override {}
    void set_text_input_area(Rect, float) override {}
    std::string clipboard_text() override { return clipboard; }
    void set_clipboard_text(const std::string& text) override { clipboard = text; }
    void wake_at(double) override {}
    void open_popup(detail::PopupRequest) override {}
    void close_menus(int) override {}
    void close_menus_then(std::function<void()> action) override { action(); }
    void close_tooltip() override {}
    void close_popup(std::uint64_t) override {}
    void move_popup(std::uint64_t, Rect) override {}
    [[nodiscard]] int menu_level() const override { return -1; }
};

// A 300 x 200 window with `view` in its top-leading corner.
struct Window {
    ClipboardServices services;
    detail::FocusManager focus{&services};
    std::unique_ptr<detail::Element> root;
    std::unique_ptr<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(view.frame(300, 200, Alignment::top_leading),
                             {.text = &text_system(), .focus = &focus, .services = &services});
        root->size_that_fits({300.f, 200.f});
        root->place({}, {300.f, 200.f});
        events = std::make_unique<detail::EventDispatcher>(*root, focus);
    }

    // The window's texts, in tree order.
    [[nodiscard]] std::vector<detail::TextElement*> texts() const {
        std::vector<detail::TextElement*> found;
        const std::function<void(detail::Element&)> visit = [&](detail::Element& element) {
            if (auto* text = dynamic_cast<detail::TextElement*>(&element)) {
                found.push_back(text);
            }
            element.for_each_child(visit);
        };
        visit(*root);
        return found;
    }
    [[nodiscard]] detail::TextElement& text(std::size_t index = 0) const { return *texts().at(index); }

    void press(Point at, int count = 1, KeyModifiers modifiers = KeyModifiers::none) const {
        events->pointer_moved(at);
        events->pointer_button(PointerEvent::Kind::down, at, MouseButton::left, count, modifiers);
        events->pointer_button(PointerEvent::Kind::up, at, MouseButton::left, count, modifiers);
    }
    void drag(Point from, Point to) const {
        events->pointer_moved(from);
        events->pointer_button(PointerEvent::Kind::down, from, MouseButton::left, 1, KeyModifiers::none);
        events->pointer_moved(to);
        events->pointer_button(PointerEvent::Kind::up, to, MouseButton::left, 1, KeyModifiers::none);
    }
};

struct Selection {
    std::size_t start = 0;
    std::size_t end = 0;
    bool operator==(const Selection&) const = default;
};

Selection selection_of(const detail::TextElement& text) {
    return {text.selection_start(), text.selection_end()};
}

// Where the text's `line` (0 first) is drawn, `x` points in from the frame's leading edge.
Point on_line(const detail::TextElement& text, float x, int line, float line_height) {
    const Rect frame = text.frame();
    return {frame.x + x, frame.y + (static_cast<float>(line) + 0.5f) * line_height};
}

// The rects the text paints in `color` (its selection's highlight).
std::vector<Rect> rects_in(const detail::TextElement& text, Color color) {
    detail::DisplayList list;
    text.paint(list);
    std::vector<Rect> found;
    for (const detail::DrawCommand& command : list.commands) {
        const auto* rect = std::get_if<detail::RectCommand>(&command);
        const Color* fill = rect ? std::get_if<Color>(&rect->fill.value()) : nullptr;
        if (fill && *fill == color) {
            found.push_back(rect->rect);
        }
    }
    return found;
}

} // namespace

TEST_CASE("a selectable Text selects what's dragged over, and the pointer is an I-beam over it", "[selectable-text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const Window window(Text("hello world").selectable());
    detail::TextElement& text = window.text();
    const Rect frame = text.frame();

    window.drag({frame.x + 0.5f, frame.y + 5}, {frame.max_x() + 50, frame.y + 5}); // past the end
    CHECK(selection_of(text) == Selection{0, 11});
    CHECK(window.focus.selected_text() == &text);
    CHECK(window.focus.focused() == nullptr); // selecting doesn't focus it

    window.events->pointer_moved({frame.x + 5, frame.y + 5});
    CHECK(window.events->pointer_style() == PointerStyle::text);
}

TEST_CASE("a double press selects a word, a triple press a paragraph, Shift extends", "[selectable-text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const Window window(Text("hello world\nsecond line").selectable());
    const detail::TextElement& text = window.text();
    const float line = text.frame().height / 2;

    window.press(on_line(text, 2, 0, line), 2);
    CHECK(selection_of(text) == Selection{0, 5});
    window.press(on_line(text, 2, 1, line), 3);
    CHECK(selection_of(text) == Selection{12, 23});

    window.press(on_line(text, 0, 0, line));
    CHECK(selection_of(text) == Selection{0, 0}); // a press alone selects nothing
    window.press(on_line(text, text.frame().width - 0.5f, 1, line), 1, KeyModifiers::shift); // the end
    CHECK(selection_of(text) == Selection{0, 23});
}

TEST_CASE("positions follow the text's alignment, and stop where a line limit cuts it off", "[selectable-text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    // Centered: the first line, two letters, is drawn one letter in from the frame's edge (the
    // second line, four, fills it). The frame's middle is between its letters.
    const Window centered(Text("mm\nmmmm").selectable().multiline_text_alignment(TextAlignment::center));
    const detail::TextElement& lines = centered.text();
    const Rect frame = lines.frame();
    const float line = frame.height / 2;
    centered.drag(on_line(lines, frame.width / 2, 0, line), on_line(lines, frame.width + 10, 0, line));
    CHECK(selection_of(lines) == Selection{1, 2});

    const std::string words = "one two three four five six seven eight nine ten eleven twelve";
    const Window limited(Text(words).selectable().line_limit(1).frame(120, 40));
    const detail::TextElement& cut = limited.text();
    limited.press({cut.frame().x + 2, cut.frame().y + 5}, 3); // the whole paragraph, as far as it shows
    CHECK(cut.selection_start() == 0);
    CHECK(cut.selection_end() > 0);
    CHECK(cut.selection_end() < words.size());
}

TEST_CASE("a Text is selectable only with the modifier, and while its value is true", "[selectable-text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const Window plain(Text("plain"));
    CHECK(plain.text().event_handler() == nullptr);

    const State<bool> selectable(true);
    const Window window(Text("toggled").selectable(Prop<bool>([selectable] { return selectable(); })));
    const detail::TextElement& text = window.text();
    const Rect frame = text.frame();
    window.drag({frame.x + 0.5f, frame.y + 5}, {frame.max_x() + 10, frame.y + 5});
    CHECK(selection_of(text) == Selection{0, 7});

    selectable.set(false);
    detail::flush_effects();
    CHECK(selection_of(text) == Selection{0, 0}); // no longer selectable: the selection goes
    window.drag({frame.x + 0.5f, frame.y + 5}, {frame.max_x() + 10, frame.y + 5});
    CHECK(selection_of(text) == Selection{0, 0});
}

TEST_CASE("a selection is drawn in the tint, at a text field's selection opacity", "[selectable-text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const Window window(Text("hello world").selectable().tint(kRed));
    const detail::TextElement& text = window.text();
    Color highlight = kRed;
    highlight.a *= detail::kSelectionOpacity;
    CHECK(rects_in(text, highlight).empty());

    const Rect frame = text.frame();
    window.drag({frame.x + 0.5f, frame.y + 5}, {frame.max_x() + 10, frame.y + 5});
    const std::vector<Rect> rects = rects_in(text, highlight);
    REQUIRE(rects.size() == 1);
    CHECK(rects[0].x == frame.x);
    CHECK(rects[0].height == frame.height);
}

TEST_CASE("a selection in one Text clears another's", "[selectable-text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const Window window(VStack(Text("first").selectable(), Text("second").selectable()));
    const detail::TextElement& first = window.text(0);
    detail::TextElement& second = window.text(1);
    window.drag({first.frame().x + 0.5f, first.frame().y + 5}, {first.frame().max_x() + 10, first.frame().y + 5});
    REQUIRE(selection_of(first) == Selection{0, 5});

    window.drag({second.frame().x + 0.5f, second.frame().y + 5}, {second.frame().max_x() + 10, second.frame().y + 5});
    CHECK(selection_of(second) == Selection{0, 6});
    CHECK(selection_of(first) == Selection{0, 0});
    CHECK(window.focus.selected_text() == &second);
}
