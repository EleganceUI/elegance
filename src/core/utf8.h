// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// UTF-8 that may be malformed: text from apps, the clipboard, input methods, drops and bindings.
// A malformed stretch becomes U+FFFD, one for each maximal subpart: the longest start of a
// well-formed sequence (Unicode's table 3-7) at that point, or a single byte that can't start one.
// So "\xE2\x82" (cut short) is one U+FFFD, "\xC0\xAF" (overlong) is two, and "\xED\xA0\x80" (a
// surrogate) is three.

#ifndef ELG_CORE_UTF8_H
#define ELG_CORE_UTF8_H

#include <cstddef>
#include <string>
#include <string_view>

namespace elg::detail {

inline constexpr char32_t kReplacementCharacter = 0xFFFD;

// The character at `at` (which must be < text.size()), advancing `at` past it; a maximal subpart
// decodes as U+FFFD and `at` goes past it. Reads no byte at or after text.size().
char32_t decode_utf8(std::string_view text, std::size_t& at) noexcept;

// Whether `text` is well-formed UTF-8.
bool is_valid_utf8(std::string_view text) noexcept;

// `text` with each maximal subpart replaced by U+FFFD (EF BF BD).
std::string valid_utf8(std::string_view text);
// The same, in place; leaves well-formed text untouched (and unallocated).
void make_valid_utf8(std::string& text);

} // namespace elg::detail

#endif // ELG_CORE_UTF8_H
