// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// TextField (one line, or a few with line_limit; secure) and TextArea share one TextEditor.

#include "text/text_system.h"
#include "view/text_field.h"
#include "view/view_node.h"

#include <elg/error.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace elg;
using detail::TextEditor;

namespace {

struct Fixture {
    detail::Scope scope;
    detail::Scope::Use use{scope};
    detail::TextSystem text;
    State<std::string> value;
    int submits = 0;

    TextEditor editor(bool area = false) {
        TextEditor made(text, value.binding(), [this] { ++submits; }, nullptr, nullptr, {}, area);
        made.set_font(Font::system(15));
        return made;
    }
};

void type(TextEditor& editor, std::string text) {
    editor.text_input({.kind = TextInputEvent::Kind::commit, .text = std::move(text)});
}

void press(TextEditor& editor, Key key, KeyModifiers modifiers = KeyModifiers::none) {
    editor.key({.kind = KeyEvent::Kind::down, .key = key, .modifiers = modifiers});
}

} // namespace

TEST_CASE("a secure field hides its text until it's revealed", "[text-input]") {
    Fixture f;
    TextEditor editor = f.editor();
    editor.set_secure(true);
    type(editor, "hunter2");
    CHECK(f.value() == "hunter2");
    CHECK(editor.accessible_text().empty());

    editor.set_secure(false);
    CHECK(editor.accessible_text() == "hunter2");
    CHECK(f.value() == "hunter2"); // revealing doesn't edit
}

TEST_CASE("a field takes new lines only with a line limit, and only with Shift+Enter", "[text-input]") {
    Fixture f;
    TextEditor editor = f.editor();
    type(editor, "one");
    press(editor, Key::enter, KeyModifiers::shift); // one line: it submits like Enter
    CHECK(f.value() == "one");
    CHECK(f.submits == 1);

    editor.set_line_limit(3);
    press(editor, Key::enter, KeyModifiers::shift);
    type(editor, "two");
    CHECK(f.value() == "one\ntwo");
    CHECK(f.submits == 1);
    press(editor, Key::enter);
    CHECK(f.submits == 2);
}

TEST_CASE("a field that becomes one line or secure joins its lines", "[text-input]") {
    Fixture f;
    TextEditor editor = f.editor();
    editor.set_line_limit(3);
    type(editor, "one");
    press(editor, Key::enter, KeyModifiers::shift);
    type(editor, "two");
    REQUIRE(f.value() == "one\ntwo");

    editor.set_secure(true);
    CHECK(f.value().find('\n') == std::string::npos);
}

TEST_CASE("a text area starts new lines on Enter and submits on the primary modifier", "[text-input]") {
    Fixture f;
    TextEditor editor = f.editor(true);
    type(editor, "one");
    press(editor, Key::enter);
    type(editor, "two");
    CHECK(f.value() == "one\ntwo");
    CHECK(f.submits == 0);
    press(editor, Key::enter, primary_modifier());
    CHECK(f.submits == 1);
    CHECK(f.value() == "one\ntwo");
}

TEST_CASE("a text area takes the height it's offered, or its text's", "[text-input]") {
    Fixture f;
    TextEditor editor = f.editor(true);
    const float one_line = editor.measure({200.f, std::nullopt}).height;
    CHECK(one_line > 0);
    type(editor, "one");
    press(editor, Key::enter);
    type(editor, "two");
    CHECK(editor.measure({200.f, std::nullopt}).height > one_line);
    CHECK(editor.measure({200.f, 300.f}).height == 300.f);
}

TEST_CASE("a fixed secure field can't have several lines", "[text-input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::string> password;
    CHECK_THROWS_AS(TextField("Password", password.binding(), {.secure = true, .line_limit = 3}), UsageError);
    CHECK_NOTHROW(TextField("Password", password.binding(), {.secure = true}));
    CHECK_NOTHROW(TextArea("Notes", password.binding()));
}
