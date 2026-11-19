// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Words and paragraphs, as double and triple clicks select them.

#include "text/text_boundaries.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <utility>

using elg::detail::paragraph_around;
using elg::detail::word_around;

namespace {

using Range = std::pair<std::size_t, std::size_t>;

} // namespace

TEST_CASE("a word is the run of word characters, or of others, around a byte", "[text-boundaries]") {
    constexpr std::string_view text = "say hello_world, ok";
    CHECK(word_around(text, 6) == Range{4, 15}); // in "hello_world"
    CHECK(word_around(text, 4) == Range{4, 15}); // at its start
    CHECK(word_around(text, 15) == Range{15, 17}); // ", " between words
    CHECK(word_around(text, text.size()) == Range{17, 19}); // the end: the word it ends
    CHECK(word_around("", 0) == Range{0, 0});
}

TEST_CASE("a word doesn't cross a line break, and keeps non-ASCII characters whole", "[text-boundaries]") {
    constexpr std::string_view text = "one\ntwo";
    CHECK(word_around(text, 1) == Range{0, 3});
    CHECK(word_around(text, 5) == Range{4, 7});
    // "café au": é is two bytes; a byte inside it still gives the whole word.
    constexpr std::string_view accented = "caf\xC3\xA9 au";
    CHECK(word_around(accented, 4) == Range{0, 5});
}

TEST_CASE("a paragraph runs between the line breaks around a byte", "[text-boundaries]") {
    constexpr std::string_view text = "first\nsecond line\nthird";
    CHECK(paragraph_around(text, 2) == Range{0, 5});
    CHECK(paragraph_around(text, 5) == Range{0, 5}); // at the line break: the paragraph it ends
    CHECK(paragraph_around(text, 6) == Range{6, 17}); // just after it: the next one
    CHECK(paragraph_around(text, text.size()) == Range{18, 23});
    CHECK(paragraph_around("\n\n", 1) == Range{1, 1}); // an empty paragraph
}
