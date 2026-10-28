// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Secure text fields start the window's text input as a password's (no IME), and switch it
// when they're revealed or hidden while focused.

#include "text/text_system.h"
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"
#include "view/window_services.h"

#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace elg;

namespace {

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// Records the text input calls: "start", "start secure", "stop".
class RecordingServices final : public detail::WindowServices {
public:
    std::vector<std::string> calls;

    void start_text_input(bool secure) override { calls.emplace_back(secure ? "start secure" : "start"); }
    void stop_text_input() override { calls.emplace_back("stop"); }
    void set_text_input_area(Rect, float) override {}
    std::string clipboard_text() override { return {}; }
    void set_clipboard_text(const std::string&) override {}
    void wake_at(double) override {}
    void open_popup(detail::PopupRequest) override {}
    void close_menus(int) override {}
    void close_menus_then(std::function<void()> action) override { action(); }
    void close_tooltip() override {}
    void close_popup(std::uint64_t) override {}
    void move_popup(std::uint64_t, Rect) override {}
    int menu_level() const override { return -1; }
};

struct Window {
    RecordingServices services;
    detail::FocusManager focus{&services};
    std::unique_ptr<detail::Element> root;
    std::optional<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(view.frame(200, 60, Alignment::top_leading),
                             {.text = &text_system(), .focus = &focus, .services = &services});
        root->size_that_fits({200, 60});
        root->place({}, {200, 60});
        events.emplace(*root, focus);
    }
    void click(Point at) {
        events->pointer_button(PointerEvent::Kind::down, at, MouseButton::left, 1, KeyModifiers::none);
        events->pointer_button(PointerEvent::Kind::up, at, MouseButton::left, 1, KeyModifiers::none);
        detail::flush_effects();
    }
};

} // namespace

TEST_CASE("a secure field's text input is a password's", "[text-input][secure]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::string> name;
    const State<std::string> password;
    Window window(VStack({.spacing = 0}, TextField("Name", name.binding()).frame(200, 30),
                         TextField("Password", password.binding(), {.secure = true}).frame(200, 30)));
    window.click({100, 15});
    CHECK(window.services.calls == std::vector<std::string>{"start"});
    window.click({100, 45});
    CHECK(window.services.calls == std::vector<std::string>{"start", "stop", "start secure"});
    window.click({100, 15});
    CHECK(window.services.calls == std::vector<std::string>{"start", "stop", "start secure", "stop", "start"});
}

TEST_CASE("revealing a focused password switches its text input", "[text-input][secure]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::string> password;
    const State<bool> reveal(false);
    Window window(TextField("Password", password.binding(), {.secure = [=] { return !reveal(); }}).frame(200, 30));
    window.click({100, 15});
    CHECK(window.services.calls == std::vector<std::string>{"start secure"});
    reveal = true;
    detail::flush_effects();
    CHECK(window.services.calls == std::vector<std::string>{"start secure", "stop", "start"});
    reveal = false;
    detail::flush_effects();
    CHECK(window.services.calls == std::vector<std::string>{"start secure", "stop", "start", "stop", "start secure"});

    // Focus leaving stops it.
    window.focus.focus(nullptr, false);
    CHECK(window.services.calls.back() == "stop");
}
