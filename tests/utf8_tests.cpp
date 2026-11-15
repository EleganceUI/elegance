// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Malformed UTF-8 becomes U+FFFD, one per maximal subpart, in the decoder and wherever text comes
// into Elegance: Text, StyledText, text fields (from their binding, typing, an input method or
// pasting), and the accessibility tree.

#include "core/utf8.h"
#include "text/text_system.h"
#include "view/accessibility.h"
#include "view/text_field.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace elg;

namespace {

constexpr std::string_view kFFFD = "\xEF\xBF\xBD";

// `text` with each '#' replaced by U+FFFD.
std::string replaced(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '#') {
            out += kFFFD;
        } else {
            out += c;
        }
    }
    return out;
}

std::vector<char32_t> decode_all(std::string_view text) {
    std::vector<char32_t> out;
    for (std::size_t at = 0; at < text.size();) {
        out.push_back(detail::decode_utf8(text, at));
    }
    return out;
}

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

} // namespace

TEST_CASE("UTF-8 decodes, and each maximal subpart becomes U+FFFD", "[utf8]") {
    CHECK(decode_all("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80") == std::vector<char32_t>{'a', 0xE9, 0x20AC, 0x1F600});
    CHECK(detail::is_valid_utf8("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xEF\xBF\xBD"));
    // The example of the Unicode Standard's section 3.9 (U+FFFD substitution of maximal subparts).
    const std::string_view example = "\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64";
    CHECK_FALSE(detail::is_valid_utf8(example));
    CHECK(detail::valid_utf8(example) == replaced("a###b#c##d"));
    CHECK(decode_all(example) == std::vector<char32_t>{'a', 0xFFFD, 0xFFFD, 0xFFFD, 'b', 0xFFFD, 'c', 0xFFFD, 0xFFFD, 'd'});

    CHECK(detail::valid_utf8("\xC0\xAF") == replaced("##"));               // overlong, C0 and C1 never lead
    CHECK(detail::valid_utf8("\xE0\x80\xAF") == replaced("###"));          // overlong
    CHECK(detail::valid_utf8("\xF0\x80\x80\xAF") == replaced("####"));     // overlong
    CHECK(detail::valid_utf8("\xED\xA0\x80") == replaced("###"));          // a surrogate
    CHECK(detail::valid_utf8("\xF4\x90\x80\x80") == replaced("####"));     // past U+10FFFF
    CHECK(detail::valid_utf8("\xF5\xFF") == replaced("##"));               // bytes that never occur
    CHECK(detail::valid_utf8("x\xE2\x82") == replaced("x#"));              // cut short by the end: one
    CHECK(detail::valid_utf8("\xF0\x9F\x98x") == replaced("#x"));          // cut short by the next character
    CHECK(detail::valid_utf8("\xE2\x82\xAC\x80") == "\xE2\x82\xAC" + replaced("#")); // a stray continuation
    CHECK(detail::valid_utf8(std::string_view("\0\xFF", 2)) == std::string("\0", 1) + replaced("#"));

    std::string in_place = "ok";
    detail::make_valid_utf8(in_place);
    CHECK(in_place == "ok");
    in_place = "\xFFok";
    detail::make_valid_utf8(in_place);
    CHECK(in_place == replaced("#ok"));
}

TEST_CASE("decoding stops at the end of the text it's given", "[utf8]") {
    // A view of the first two bytes of a three-byte character: the third isn't read.
    const std::string whole = "\xE2\x82\xAC";
    std::size_t at = 0;
    CHECK(detail::decode_utf8(std::string_view(whole).substr(0, 2), at) == 0xFFFD);
    CHECK(at == 2);
}

TEST_CASE("Text and StyledText show and expose the replaced text", "[utf8][text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const auto label = [](const std::unique_ptr<detail::Element>& element) {
        detail::AccessibilityInfo info;
        element->accessibility(info);
        return info.label;
    };
    const std::unique_ptr<detail::Element> text = detail::lower(Text("a\xE2\x82z\xFF"), {.text = &text_system()});
    CHECK(label(text) == replaced("a#z#"));

    // Each run on its own: a character can't start in one run and end in the next.
    const std::unique_ptr<detail::Element> styled =
        detail::lower(StyledText({{.text = "\xE2\x82"}, {.text = "\xAC!"}}), {.text = &text_system()});
    CHECK(label(styled) == replaced("##!"));
}

TEST_CASE("text fields edit the replaced text", "[utf8][text-input]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<std::string> value(std::string("ab\xC3"));
    detail::TextEditor editor(text_system(), value.binding(), {}, nullptr, nullptr, {}, true);
    editor.set_font(Font::system(15));
    editor.set_text(value());
    CHECK(editor.accessible_text() == replaced("ab#"));

    // Typing (and pasting, which goes the same way) after it.
    editor.text_input({.kind = TextInputEvent::Kind::commit, .text = "\xF0\x9F!"});
    CHECK(editor.accessible_text() == replaced("ab##!"));
    CHECK(value() == replaced("ab##!")); // what the binding gets back
    CHECK(detail::is_valid_utf8(value()));

    // An input method's composition, and a screen reader setting the value.
    editor.text_input({.kind = TextInputEvent::Kind::composition, .text = "\xE3\x81", .cursor = 2});
    CHECK(detail::is_valid_utf8(editor.accessible_text()));
    editor.replace_all("\xC1\x81x");
    CHECK(editor.accessible_text() == replaced("##x"));
}

TEST_CASE("the accessibility tree gets the replaced text", "[utf8][accessibility]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const std::unique_ptr<detail::Element> root =
        detail::lower(Text("ok").accessibility_label("bad \xE0\x80 label"), {.text = &text_system()});
    root->size_that_fits({100.f, 100.f});
    root->place({}, {100.f, 100.f});
    const detail::AccessTree tree = detail::build_accessibility_tree(*root, "Test \xFF", {100, 100}, nullptr);
    for (const detail::AccessNode& node : tree.nodes) {
        CHECK(detail::is_valid_utf8(node.info.label));
        CHECK(detail::is_valid_utf8(node.info.value));
    }
}
