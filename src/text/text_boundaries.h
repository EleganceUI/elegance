// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_TEXT_BOUNDARIES_H
#define ELG_TEXT_TEXT_BOUNDARIES_H

// Words and paragraphs in UTF-8 text, as text fields and selectable Text select them with double
// and triple clicks. Offsets are bytes.

#include <cstddef>
#include <string_view>
#include <utility>

namespace elg::detail {

// Word characters, for word movement and double clicks: letters, digits, '_', and anything
// non-ASCII (so words in other scripts hold together).
[[nodiscard]] bool is_word_byte(char c) noexcept;

// `byte` (at most the text's size) moved back onto the start of a UTF-8 character.
[[nodiscard]] std::size_t character_boundary(std::string_view text, std::size_t byte) noexcept;

// The run of word characters, or of other characters, around `byte`, which doesn't cross a line
// break ('\n'): [begin, end), on character boundaries.
[[nodiscard]] std::pair<std::size_t, std::size_t> word_around(std::string_view text, std::size_t byte) noexcept;

// The paragraph around `byte`: from the line break before it (or the text's start) to the one
// after it (or the text's end), without the line breaks.
[[nodiscard]] std::pair<std::size_t, std::size_t> paragraph_around(std::string_view text,
                                                                   std::size_t byte) noexcept;

} // namespace elg::detail

#endif // ELG_TEXT_TEXT_BOUNDARIES_H
